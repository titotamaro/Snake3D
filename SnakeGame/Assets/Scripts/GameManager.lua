-- Drives the Snake game: input, timing, spawning/positioning entities, scoring, audio, HUD and the game-over effect.
-- The rules themselves live in SnakeLogic.lua.

local SnakeLogic = require("SnakeLogic")

local GameManager = {
	Properties = {
		GridSize = 20,
		InitialLength = 4,
		StepTime = 0.16,      -- seconds per move at the start
		MinStepTime = 0.065,  -- fastest speed
		SpeedUp = 0.004,      -- step time reduction per apple
		CameraFollow = 0.18,  -- how much the camera drifts towards the head
		MusicVolume = 0.35,
	},
}

local KeyDirections = {
	{ keys = { Key.Up, Key.W }, direction = SnakeLogic.Directions.Up },
	{ keys = { Key.Down, Key.S }, direction = SnakeLogic.Directions.Down },
	{ keys = { Key.Left, Key.A }, direction = SnakeLogic.Directions.Left },
	{ keys = { Key.Right, Key.D }, direction = SnakeLogic.Directions.Right },
}

local SegmentHeight = 0.4

local function Lerp(a, b, t)
	return a + (b - a) * t
end

local function LerpAngle(a, b, t)
	local delta = (b - a + math.pi) % (2 * math.pi) - math.pi
	return a + delta * t
end

local function DirectionYaw(direction)
	-- Models face -Z; grid "up" (y - 1) is world -Z.
	return math.atan(-direction.x, -direction.y)
end

function GameManager:CellToWorld(cell, height)
	local offset = (self.GridSize - 1) / 2
	return vec3(cell.x - offset, height or SegmentHeight, cell.y - offset)
end

function GameManager:OnCreate()
	self.Head = Scene.FindEntityByName("Snake Head")
	self.Apple = Scene.FindEntityByName("Apple")
	self.Camera = Scene.GetPrimaryCamera()
	assert(self.Head, "the scene needs an entity named 'Snake Head'")
	assert(self.Apple, "the scene needs an entity named 'Apple'")

	if self.Camera then
		self.CameraBase = self.Camera.Transform.Translation
	end

	self.HighScore = 0
	self.Segments = {}
	self:BuildWalls()

	if self.Entity.AudioSource then
		self.Entity.AudioSource.Volume = self.MusicVolume
	end

	self:Reset()
	Log.Info("Snake ready: press an arrow key or WASD to start")
end

function GameManager:BuildWalls()
	local size = self.GridSize
	local function AddWall(x, y)
		local wall = Scene.CreateEntity("Wall")
		wall.Transform.Translation = self:CellToWorld({ x = x, y = y }, 0.5)
		wall:AddComponent("MeshRenderer", { Mesh = "Meshes/Wall.gltf" })
		wall:AddComponent("BoxCollider", { HalfExtents = vec3(0.5, 0.5, 0.5) })
	end
	for i = -1, size do
		AddWall(i, -1)
		AddWall(i, size)
	end
	for i = 0, size - 1 do
		AddWall(-1, i)
		AddWall(size, i)
	end
end

function GameManager:Reset()
	-- Remove debris from a previous game.
	for _, segment in ipairs(self.Segments) do
		segment:Destroy()
	end
	self.Segments = {}
	if self.Head:HasComponent("RigidBody") then
		self.Head:RemoveComponent("RigidBody")
		self.Head:RemoveComponent("BoxCollider")
	end

	self.State = SnakeLogic.NewGame(self.GridSize, self.InitialLength, Random.Int)
	self.PreviousBody = SnakeLogic.CopyBody(self.State)
	self.Timer = 0
	self.Mode = "Ready"
	self.GameOverTime = 0
	self.HeadYaw = DirectionYaw(self.State.Direction)
	self.PreviousYaw = self.HeadYaw

	self:SyncSegments()
	self:UpdateVisuals(1.0)
	self:PlaceApple()
end

function GameManager:CurrentStepTime()
	return math.max(self.MinStepTime, self.StepTime - self.SpeedUp * self.State.Score)
end

-- Keeps one body entity per snake segment after the head.
function GameManager:SyncSegments()
	local needed = #self.State.Body - 1
	while #self.Segments < needed do
		local segment = Scene.CreateEntity("Snake Segment")
		segment:AddComponent("MeshRenderer", { Mesh = "Meshes/SnakeBody.gltf" })
		local tail = self.PreviousBody[#self.PreviousBody]
		segment.Transform.Translation = self:CellToWorld(tail)
		self.Segments[#self.Segments + 1] = segment
	end
	while #self.Segments > needed do
		table.remove(self.Segments):Destroy()
	end
end

function GameManager:PlaceApple()
	if self.State.Apple then
		local position = self:CellToWorld(self.State.Apple, 0.45)
		self.Apple.Transform.Translation = position
		self.Apple.MeshRenderer.Visible = true
	else
		self.Apple.MeshRenderer.Visible = false
	end
end

function GameManager:UpdateVisuals(t)
	local body = self.State.Body
	local previous = self.PreviousBody

	for i = 1, #body do
		local from = previous[i] or previous[#previous]
		local to = body[i]
		local a = self:CellToWorld(from)
		local b = self:CellToWorld(to)
		local position = vec3(Lerp(a.x, b.x, t), SegmentHeight, Lerp(a.z, b.z, t))

		if i == 1 then
			self.Head.Transform.Translation = position
			self.Head.Transform.Rotation = vec3(0, LerpAngle(self.PreviousYaw, self.HeadYaw, math.min(1, t * 1.5)), 0)
		else
			local segment = self.Segments[i - 1]
			-- Taper towards the tail.
			local scale = 1.0 - 0.35 * ((i - 1) / math.max(#body - 1, 1))
			segment.Transform.Translation = vec3(position.x, SegmentHeight * scale, position.z)
			segment.Transform.Scale = vec3(scale, scale, scale)
			local ahead = body[i - 1]
			segment.Transform.Rotation = vec3(0, DirectionYaw({ x = ahead.x - to.x, y = ahead.y - to.y }), 0)
		end
	end
end

function GameManager:HandleInput()
	for _, binding in ipairs(KeyDirections) do
		for _, key in ipairs(binding.keys) do
			if Input.IsKeyPressed(key) then
				if SnakeLogic.QueueDirection(self.State, binding.direction) then
					Audio.PlayOneShot("Audio/Turn.wav", 0.4)
				end
				return true
			end
		end
	end
	return false
end

function GameManager:Explode()
	-- Every segment becomes a physics body flung away from the head.
	local origin = self.Head.Transform.Translation
	local pieces = { self.Head }
	for _, segment in ipairs(self.Segments) do
		pieces[#pieces + 1] = segment
	end
	for index, piece in ipairs(pieces) do
		local scale = piece.Transform.Scale
		piece:AddComponent("BoxCollider", { HalfExtents = vec3(0.4, 0.33, 0.4), Restitution = 0.3, Friction = 0.6 })
		piece:AddComponent("RigidBody", { Type = "Dynamic", Mass = 1.0 })
		local away = piece.Transform.Translation - origin
		if away:Length() < 0.01 then
			away = vec3(Random.Float(-1, 1), 0, Random.Float(-1, 1))
		end
		local impulse = away:Normalized() * Random.Float(2.0, 4.0) + vec3(0, Random.Float(4.0, 7.0) - index * 0.05, 0)
		piece.RigidBody:AddImpulse(impulse * scale.x)
		piece.RigidBody.AngularVelocity = vec3(Random.Float(-6, 6), Random.Float(-6, 6), Random.Float(-6, 6))
	end
end

function GameManager:OnUpdate(dt)
	if self.Mode == "Ready" then
		if self:HandleInput() or Input.IsKeyPressed(Key.Space) then
			self.Mode = "Playing"
		end
	elseif self.Mode == "Playing" then
		self:HandleInput()
		if Input.IsKeyPressed(Key.P) or Input.IsKeyPressed(Key.Escape) then
			self.Mode = "Paused"
		end

		self.Timer = self.Timer + dt
		local stepTime = self:CurrentStepTime()
		while self.Mode == "Playing" and self.Timer >= stepTime do
			self.Timer = self.Timer - stepTime
			self.PreviousBody = SnakeLogic.CopyBody(self.State)
			self.PreviousYaw = self.HeadYaw

			local event = SnakeLogic.Step(self.State)
			if event == "dead" then
				self:OnGameOver(false)
			else
				self.HeadYaw = DirectionYaw(self.State.Direction)
				self:SyncSegments()
				if event == "eat" or event == "win" then
					Audio.PlayOneShot("Audio/Eat.wav", 0.7)
					self:PlaceApple()
				end
				if event == "win" then
					self:OnGameOver(true)
				end
			end
		end

		if self.Mode == "Playing" then
			self:UpdateVisuals(math.min(self.Timer / self:CurrentStepTime(), 1.0))
		end
	elseif self.Mode == "Paused" then
		if Input.IsKeyPressed(Key.P) or Input.IsKeyPressed(Key.Escape) or Input.IsKeyPressed(Key.Space) then
			self.Mode = "Playing"
		end
	elseif self.Mode == "GameOver" then
		self.GameOverTime = self.GameOverTime + dt
		if self.GameOverTime > 1.0 and (Input.IsKeyPressed(Key.Space) or Input.IsKeyPressed(Key.Enter) or Input.IsKeyPressed(Key.R)) then
			self:Reset()
		end
	end

	self:UpdateCamera(dt)
	self:DrawHUD()
end

function GameManager:OnGameOver(won)
	self.Mode = "GameOver"
	self.Won = won
	self.GameOverTime = 0
	self.HighScore = math.max(self.HighScore, self.State.Score)
	self:UpdateVisuals(1.0)
	if won then
		Audio.PlayOneShot("Audio/Eat.wav", 1.0)
	else
		Audio.PlayOneShot("Audio/GameOver.wav", 0.8)
		self:Explode()
	end
	Log.Info("Game over - score " .. self.State.Score)
end

function GameManager:UpdateCamera(dt)
	if not self.Camera or not self.CameraBase then
		return
	end
	local head = self.Head.Transform.Translation
	local target = vec3(self.CameraBase.x + head.x * self.CameraFollow, self.CameraBase.y, self.CameraBase.z + head.z * self.CameraFollow)
	local current = self.Camera.Transform.Translation
	self.Camera.Transform.Translation = current:Lerp(target, math.min(1.0, dt * 3.0))
end

function GameManager:DrawHUD()
	local white = vec4(1, 1, 1, 1)
	local gold = vec4(1.0, 0.85, 0.3, 1)
	UI.Text("Score  " .. self.State.Score, 28, 20, { Size = 34, Color = white })
	UI.Text("Best  " .. self.HighScore, -28, 20, { Size = 34, Color = gold, Anchor = "TopRight" })

	if self.Mode == "Ready" then
		UI.Text("SNAKE 3D", 0, -70, { Size = 72, Color = vec4(0.55, 1.0, 0.45, 1), Anchor = "Center" })
		UI.Text("Arrow keys / WASD to move   -   P to pause", 0, 10, { Size = 26, Color = white, Anchor = "Center" })
		UI.Text("Press an arrow key to start", 0, 50, { Size = 26, Color = gold, Anchor = "Center" })
	elseif self.Mode == "Paused" then
		UI.Text("PAUSED", 0, 0, { Size = 64, Color = white, Anchor = "Center" })
	elseif self.Mode == "GameOver" then
		local title = self.Won and "YOU WIN!" or "GAME OVER"
		UI.Text(title, 0, -40, { Size = 72, Color = self.Won and gold or vec4(1, 0.35, 0.3, 1), Anchor = "Center" })
		UI.Text("Score " .. self.State.Score, 0, 25, { Size = 34, Color = white, Anchor = "Center" })
		if self.GameOverTime > 1.0 then
			UI.Text("Press Space to play again", 0, 70, { Size = 26, Color = gold, Anchor = "Center" })
		end
	end
	UI.Text("Speed " .. string.format("%.1f", 1.0 / self:CurrentStepTime()) .. " cells/s", 28, -24, { Size = 20, Color = vec4(1, 1, 1, 0.7), Anchor = "BottomLeft" })
end

return GameManager
