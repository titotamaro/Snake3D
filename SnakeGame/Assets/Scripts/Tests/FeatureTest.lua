-- Exercises every component and the complete scripting API. Run with:
--   OuroborosRuntime --project SnakeGame/Snake.oproj --scene Scenes/FeatureTest.oscene --headless --test
-- Failures are reported with Log.Error (which makes `--test` exit with code 1); the script quits when done.

local FeatureTest = {
	Properties = {
		SimulationFrames = 180, -- frames to simulate before checking physics results (3 s at 60 Hz)
		Label = "default",
	},
}

local Failures = 0
local Checks = 0

local function Check(condition, message)
	Checks = Checks + 1
	if not condition then
		Failures = Failures + 1
		Log.Error("FAILED: " .. message)
	end
end

local function Near(a, b, epsilon)
	return math.abs(a - b) <= (epsilon or 1e-4)
end

local function NearVec3(a, b, epsilon)
	return Near(a.x, b.x, epsilon) and Near(a.y, b.y, epsilon) and Near(a.z, b.z, epsilon)
end

local function Raises(fn)
	local ok = pcall(fn)
	return not ok
end

-- ---------------------------------------------------------------------------------------------------------------------

function FeatureTest:TestMath()
	local a = vec3(1, 2, 3)
	local b = vec3(4, 5, 6)
	Check(vec3() == vec3(0, 0, 0), "vec3() is zero")
	Check(vec3(2) == vec3(2, 2, 2), "vec3(scalar)")
	Check(a + b == vec3(5, 7, 9), "vec3 addition")
	Check(b - a == vec3(3, 3, 3), "vec3 subtraction")
	Check(a * 2 == vec3(2, 4, 6) and 2 * a == vec3(2, 4, 6), "vec3 scalar multiplication")
	Check(a * b == vec3(4, 10, 18), "vec3 component multiplication")
	Check(b / 2 == vec3(2, 2.5, 3) and b / vec3(4, 5, 6) == vec3(1, 1, 1), "vec3 division")
	Check(-a == vec3(-1, -2, -3), "vec3 negation")
	Check(Near(vec3(3, 4, 0):Length(), 5), "vec3 Length")
	Check(NearVec3(vec3(0, 0, 9):Normalized(), vec3(0, 0, 1)), "vec3 Normalized")
	Check(Near(a:Dot(b), 32), "vec3 Dot")
	Check(vec3(1, 0, 0):Cross(vec3(0, 1, 0)) == vec3(0, 0, 1), "vec3 Cross")
	Check(Near(vec3(0, 0, 0):Distance(vec3(0, 3, 4)), 5), "vec3 Distance")
	Check(NearVec3(vec3(0, 0, 0):Lerp(vec3(2, 4, 6), 0.5), vec3(1, 2, 3)), "vec3 Lerp")
	Check(tostring(vec3(1, 2, 3)):find("vec3") ~= nil, "vec3 tostring")
	a.x = 10
	Check(a.x == 10, "vec3 field assignment")

	local v2 = vec2(3, 4)
	Check(Near(v2:Length(), 5) and v2 + vec2(1) == vec2(4, 5) and v2 * 2 == vec2(6, 8), "vec2 operations")
	Check(Near(vec2(1, 0):Dot(vec2(0, 1)), 0) and Near(vec2(0, 0):Distance(vec2(3, 4)), 5), "vec2 Dot/Distance")
	Check(NearVec3(vec3(vec2(4, 0):Normalized().x, 0, 0), vec3(1, 0, 0)), "vec2 Normalized")
	Check(vec2(0, 0):Lerp(vec2(2, 2), 0.5) == vec2(1, 1) and -vec2(1, 2) == vec2(-1, -2) and vec2(4, 4) / 2 == vec2(2, 2), "vec2 misc")

	local v4 = vec4(1, 2, 3, 4)
	Check(v4.w == 4 and v4 + vec4(1) == vec4(2, 3, 4, 5) and Near(vec4(1, 0, 0, 0):Dot(v4), 1), "vec4 operations")
	Check(Near(vec4(0, 0, 3, 4):Length(), 5) and vec4(0):Lerp(vec4(2), 0.5) == vec4(1) and -v4 == vec4(-1, -2, -3, -4), "vec4 misc")
	Check((v4 - vec4(1)) == vec4(0, 1, 2, 3) and v4 * 2 == vec4(2, 4, 6, 8) and v4 / 2 == vec4(0.5, 1, 1.5, 2), "vec4 arithmetic")

	local identity = quat()
	Check(Near(identity.w, 1), "quat identity")
	local yaw = quat.AngleAxis(Math.Pi / 2, vec3(0, 1, 0))
	Check(NearVec3(yaw:Rotate(vec3(0, 0, -1)), vec3(-1, 0, 0), 1e-4), "quat AngleAxis/Rotate")
	Check(NearVec3((yaw * vec3(0, 0, -1)), vec3(-1, 0, 0), 1e-4), "quat * vec3")
	Check(NearVec3(quat.FromEuler(vec3(0, 0.5, 0)):ToEuler(), vec3(0, 0.5, 0), 1e-4), "quat FromEuler/ToEuler")
	Check(Near((yaw * identity):Normalized().w, yaw.w), "quat multiplication and Normalized")
	Check(Near(identity:Slerp(yaw, 1.0).y, yaw.y), "quat Slerp")
	Check(quat(1, 0, 0, 0) == identity and tostring(identity):find("quat") ~= nil, "quat equality/tostring")

	Check(Near(Math.Pi, math.pi, 1e-5), "Math.Pi")
	Check(Near(Math.Lerp(0, 10, 0.25), 2.5), "Math.Lerp")
	Check(Math.Clamp(5, 0, 1) == 1 and Math.Clamp(-5, 0, 1) == 0, "Math.Clamp")
	Check(Near(Math.Degrees(Math.Radians(90)), 90, 1e-3), "Math.Radians/Degrees")
	Check(Math.Sign(-3) == -1 and Math.Sign(0) == 0 and Math.Sign(2) == 1, "Math.Sign")
	Check(Math.MoveTowards(0, 10, 3) == 3 and Math.MoveTowards(9, 10, 3) == 10, "Math.MoveTowards")
end

function FeatureTest:TestCore()
	Log.Trace("FeatureTest: Log.Trace")
	Log.Info("FeatureTest: Log.Info")
	Log.Warn("FeatureTest: Log.Warn (expected)")
	print("FeatureTest: print", 1, true)

	Check(type(Ouroboros.Version) == "string" and #Ouroboros.Version > 0, "Ouroboros.Version")
	Check(Time.GetFixedDeltaTime() > 0, "Time.GetFixedDeltaTime")
	Check(Time.GetDeltaTime() >= 0, "Time.GetDeltaTime")
	Check(Time.GetElapsed() >= 0, "Time.GetElapsed")
	Check(Time.GetFrameCount() >= 0, "Time.GetFrameCount")
	Time.SetTimeScale(0.5)
	Check(Near(Time.GetTimeScale(), 0.5), "Time.SetTimeScale")
	Time.SetTimeScale(1.0)

	Random.Seed(1234)
	local first = { Random.Int(1, 1000), Random.Float(), Random.Float(5, 6) }
	Random.Seed(1234)
	local second = { Random.Int(1, 1000), Random.Float(), Random.Float(5, 6) }
	Check(first[1] == second[1] and first[2] == second[2] and first[3] == second[3], "Random.Seed is deterministic")
	Check(first[1] >= 1 and first[1] <= 1000 and first[3] >= 5 and first[3] <= 6, "Random ranges")
	Check(Raises(function() Random.Int(5, 1) end), "Random.Int rejects max < min")

	local size = Application.GetViewportSize()
	Check(size.x > 0 and size.y > 0, "Application.GetViewportSize")

	Check(Key.W ~= nil and Key.Space ~= nil and Key.D0 ~= nil and Mouse.Left == 0, "Key and Mouse tables")
	Check(Input.IsKeyDown(Key.W) == false and Input.IsKeyDown("Space") == false, "Input.IsKeyDown")
	Check(Input.IsKeyPressed(Key.W) == false and Input.IsKeyReleased(Key.W) == false, "Input.IsKeyPressed/Released")
	Check(Input.IsMouseButtonDown(Mouse.Left) == false and Input.IsMouseButtonPressed(Mouse.Right) == false, "Input mouse buttons")
	Check(Input.GetMousePosition().x ~= nil and Input.GetMouseDelta().x ~= nil and Input.GetScrollDelta().y ~= nil, "Input mouse state")
	Check(Raises(function() Input.IsKeyDown("NotAKey") end), "Input rejects unknown key names")

	Audio.SetMasterVolume(0.5)
	Check(Near(Audio.GetMasterVolume(), 0.5), "Audio master volume")
	Audio.SetMasterVolume(1.0)
	Check(Audio.PlayOneShot("Audio/Eat.wav", 0.1) == true, "Audio.PlayOneShot")

	UI.Text("Feature test", 10, 10)
	UI.Text("Centered", 0, 0, { Size = 30, Color = vec4(1, 0, 0, 1), Anchor = "Center" })
	Check(Raises(function() UI.Text("x", 0, 0, { Anchor = "Nowhere" }) end), "UI.Text rejects unknown anchors")

	local SnakeLogic = require("SnakeLogic")
	Check(type(SnakeLogic.NewGame) == "function", "require() resolves project scripts")
end

function FeatureTest:TestScene()
	Check(Scene.GetName() == "FeatureTest", "Scene.GetName")
	Check(Scene.GetEntityCount() >= 10, "Scene.GetEntityCount")
	Check(#Scene.GetEntities() == Scene.GetEntityCount(), "Scene.GetEntities")
	Check(#Scene.GetEntitiesWithComponent("MeshRenderer") >= 5, "Scene.GetEntitiesWithComponent")
	Check(Raises(function() Scene.GetEntitiesWithComponent("Bogus") end), "unknown component names raise errors")

	local camera = Scene.GetPrimaryCamera()
	Check(camera ~= nil and camera.Name == "Main Camera", "Scene.GetPrimaryCamera")
	Check(Scene.FindEntityByName("Does Not Exist") == nil, "Scene.FindEntityByName returns nil when missing")

	local me = self.Entity
	Check(Scene.GetEntityByID(me.ID) == me, "Scene.GetEntityByID and Entity equality")
	Check(tostring(me):find("FeatureTest") ~= nil, "Entity tostring")

	-- Entity lifecycle and generic component access
	local entity = Scene.CreateEntity("Scratch")
	Check(entity:IsValid() and entity.Name == "Scratch", "Scene.CreateEntity")
	entity.Name = "Renamed"
	Check(Scene.FindEntityByName("Renamed") == entity, "Entity.Name setter")
	Check(entity:HasComponent("Transform") and entity:HasComponent("Tag"), "entities always have Tag and Transform")
	Check(Raises(function() entity:RemoveComponent("Transform") end), "Transform cannot be removed")

	entity:AddComponent("MeshRenderer")
	Check(entity:HasComponent("MeshRenderer") and entity.MeshRenderer.Mesh == "Primitive:Cube", "AddComponent with defaults")
	entity:AddComponent("PointLight", { Intensity = 42, Color = vec3(1, 0, 0) })
	Check(Near(entity.PointLight.Intensity, 42) and entity.PointLight.Color == vec3(1, 0, 0), "AddComponent with data")

	local data = entity:GetComponentData("PointLight")
	Check(Near(data.Intensity, 42) and Near(data.Radius, 10), "GetComponentData")
	entity:SetComponentData("PointLight", { Radius = 3 })
	Check(Near(entity.PointLight.Radius, 3) and Near(entity.PointLight.Intensity, 42), "SetComponentData patches fields")
	Check(entity:GetComponentData("Camera") == nil, "GetComponentData of a missing component is nil")
	Check(Raises(function() entity:SetComponentData("PointLight", { Radius = "wide" }) end), "SetComponentData validates types")

	entity:RemoveComponent("PointLight")
	Check(not entity:HasComponent("PointLight") and entity.PointLight == nil, "RemoveComponent")

	local duplicate = Scene.DuplicateEntity(entity)
	Check(duplicate ~= entity and duplicate:HasComponent("MeshRenderer") and duplicate.Name == "Renamed", "Scene.DuplicateEntity")
	Scene.DestroyEntity(duplicate)
	Check(not duplicate:IsValid(), "Scene.DestroyEntity")
	Check(Raises(function() return duplicate.Name end), "accessing a destroyed entity raises an error")

	-- Scripts on entities: AddScript, properties/fields, GetScript, OnCreate/OnDestroy
	FeatureTestLifecycle = {}
	local instance = entity:AddScript("Scripts/Tests/DestroyProbe.lua", { Name = "probe", Value = 21 })
	Check(instance ~= nil and instance:Double() == 42, "Entity:AddScript with fields")
	Check(entity:GetScript() == instance, "Entity:GetScript")
	Check(instance.Offset == vec3(1, 2, 3), "script Properties defaults")
	Check(FeatureTestLifecycle[1] == "create:probe", "OnCreate called")
	entity:Destroy()
	Check(FeatureTestLifecycle[2] == "destroy:probe", "OnDestroy called on destruction")

	local scripted = Scene.CreateEntity("Scripted")
	scripted:AddComponent("Script", { Script = "Scripts/Tests/DestroyProbe.lua", Fields = { Name = "component" } })
	Check(scripted:GetScript() ~= nil and scripted:GetScript().Name == "component", "AddComponent('Script') instantiates")
	scripted:RemoveComponent("Script")
	Check(scripted:GetScript() == nil and FeatureTestLifecycle[4] == "destroy:component", "RemoveComponent('Script') destroys the instance")
	scripted:Destroy()
end

function FeatureTest:TestComponents()
	-- Transform
	local cube = Scene.FindEntityByName("Falling Cube")
	local transform = cube.Transform
	local original = transform.Translation
	transform.Translation = vec3(original.x, original.y, original.z)
	Check(NearVec3(transform.Translation, original), "Transform.Translation")
	Check(transform.Scale == vec3(1, 1, 1), "Transform.Scale")

	local marker = Scene.CreateEntity("Marker")
	marker.Transform.Rotation = vec3(0, Math.Pi / 2, 0)
	Check(NearVec3(marker.Transform:GetForward(), vec3(-1, 0, 0), 1e-4), "Transform.GetForward")
	Check(NearVec3(marker.Transform:GetRight(), vec3(0, 0, -1), 1e-4), "Transform.GetRight")
	Check(NearVec3(marker.Transform:GetUp(), vec3(0, 1, 0), 1e-4), "Transform.GetUp")
	marker.Transform:SetRotationQuat(quat())
	Check(NearVec3(marker.Transform.Rotation, vec3(0, 0, 0)), "Transform.SetRotationQuat")
	Check(Near(marker.Transform:GetRotationQuat().w, 1), "Transform.GetRotationQuat")
	marker.Transform.Translation = vec3(0, 0, 5)
	marker.Transform:LookAt(vec3(0, 0, 0))
	Check(NearVec3(marker.Transform:GetForward(), vec3(0, 0, -1), 1e-4), "Transform.LookAt")
	marker.Transform.Scale = vec3(2, 2, 2)
	Check(marker.Transform.Scale == vec3(2, 2, 2), "Transform.Scale setter")
	marker:Destroy()

	-- Camera
	local camera = Scene.FindEntityByName("Ortho Camera").Camera
	Check(camera.Projection == "Orthographic" and camera.Primary == false, "Camera projection/primary")
	camera.OrthographicSize = 12
	camera.OrthographicNear = -50
	camera.OrthographicFar = 50
	camera.FieldOfView = 60
	camera.PerspectiveNear = 0.2
	camera.PerspectiveFar = 300
	camera.Exposure = 1.5
	Check(Near(camera.OrthographicSize, 12) and Near(camera.FieldOfView, 60, 1e-3) and Near(camera.Exposure, 1.5), "Camera setters")
	Check(Near(camera.OrthographicNear, -50) and Near(camera.OrthographicFar, 50) and Near(camera.PerspectiveNear, 0.2) and Near(camera.PerspectiveFar, 300), "Camera clip planes")
	Check(Raises(function() camera.Projection = "Fisheye" end), "Camera rejects unknown projections")

	-- MeshRenderer
	local sphere = Scene.FindEntityByName("Sphere 1").MeshRenderer
	sphere.Color = vec4(1, 0.5, 0.5, 1)
	sphere.Metallic = 0.25
	sphere.Roughness = 0.75
	sphere.Emissive = vec3(0.1, 0, 0)
	sphere.EmissiveIntensity = 2
	sphere.CastShadows = true
	sphere.Visible = true
	Check(sphere.Color == vec4(1, 0.5, 0.5, 1) and Near(sphere.Metallic, 0.25) and Near(sphere.Roughness, 0.75), "MeshRenderer material overrides")
	Check(sphere.Emissive == vec3(0.1, 0, 0) and Near(sphere.EmissiveIntensity, 2) and sphere.Visible and sphere.CastShadows, "MeshRenderer emissive/visibility")
	Check(Scene.FindEntityByName("Apple Model").MeshRenderer.Mesh == "Meshes/Apple.gltf", "MeshRenderer glTF mesh path")

	-- Lights
	local sun = Scene.FindEntityByName("Sun").DirectionalLight
	sun.Intensity = sun.Intensity
	sun.Color = vec3(1, 0.95, 0.9)
	sun.ShadowSoftness = 1.2
	sun.ShadowDistance = 50
	Check(sun.CastShadows == true and Near(sun.ShadowSoftness, 1.2) and Near(sun.ShadowDistance, 50), "DirectionalLight")
	local lamp = Scene.FindEntityByName("Lamp").PointLight
	lamp.Radius = 8
	Check(Near(lamp.Radius, 8) and lamp.Intensity > 0 and lamp.Color.x >= 0, "PointLight")
	local sky = Scene.FindEntityByName("Sky").SkyLight
	sky.Rotation = 0.5
	Check(sky.Environment:find(".hdr") ~= nil and Near(sky.Rotation, 0.5) and Near(sky.Intensity, 1) and sky.ShowBackground, "SkyLight")

	-- Physics components
	local body = cube.RigidBody
	Check(body.Type == "Dynamic" and Near(body.Mass, 2) and body.FixedRotation == false, "RigidBody fields")
	body.LinearDamping = 0.05
	body.AngularDamping = 0.05
	body.GravityFactor = 1.0
	Check(Near(body.LinearDamping, 0.05) and Near(body.AngularDamping, 0.05) and Near(body.GravityFactor, 1), "RigidBody damping/gravity")
	Check(Raises(function() body.Type = "Floating" end), "RigidBody rejects unknown types")
	local box = cube.BoxCollider
	Check(box.HalfExtents == vec3(0.5, 0.5, 0.5) and box.Offset == vec3(0, 0, 0) and box.IsTrigger == false, "BoxCollider")
	Check(Near(box.Friction, 0.5) and Near(box.Restitution, 0.0), "BoxCollider material")
	local ball = Scene.FindEntityByName("Falling Ball").SphereCollider
	Check(Near(ball.Radius, 0.5) and ball.IsTrigger == false and Near(ball.Friction, 0.5) and ball.Offset == vec3(0, 0, 0), "SphereCollider")
	ball.Restitution = 0.4
	Check(Near(ball.Restitution, 0.4), "SphereCollider setter")

	-- Audio source
	local speaker = Scene.FindEntityByName("Speaker").AudioSource
	speaker.Volume = 0.2
	speaker.Pitch = 1.5
	speaker.Loop = false
	speaker.Spatial = true
	speaker.PlayOnStart = false
	Check(speaker.Clip == "Audio/Eat.wav" and Near(speaker.Volume, 0.2) and Near(speaker.Pitch, 1.5) and speaker.Spatial, "AudioSource fields")
	Check(speaker:Play() == true, "AudioSource.Play")
	speaker:IsPlaying()
	speaker:Stop()

	-- Physics API
	local gravity = Physics.GetGravity()
	Check(Near(gravity.y, -9.81, 0.01), "Physics.GetGravity")
	Physics.SetGravity(vec3(0, -9.81, 0))
	local hit = Physics.Raycast(vec3(-6, 5, 0), vec3(0, -1, 0), 20)
	Check(hit ~= nil and hit.Entity.Name == "Floor" and Near(hit.Position.y, 0, 0.05) and hit.Distance > 4, "Physics.Raycast hits the floor")
	Check(hit ~= nil and NearVec3(hit.Normal, vec3(0, 1, 0), 0.01), "Physics.Raycast normal")
	Check(Physics.Raycast(vec3(0, 50, 0), vec3(0, 1, 0), 10) == nil, "Physics.Raycast miss returns nil")
end

-- ---------------------------------------------------------------------------------------------------------------------

function FeatureTest:OnCreate()
	self.Frames = 0
	self.FixedUpdates = 0
	FeatureTestEvents = {}
	Check(self.Label == "feature", "script Fields override Properties")

	self:TestMath()
	self:TestCore()
	self:TestScene()
	self:TestComponents()

	-- Physics interactions checked after the simulation has run.
	local ball = Scene.FindEntityByName("Falling Ball")
	ball.RigidBody:AddImpulse(vec3(0, 0, 0))
	ball.RigidBody.AngularVelocity = vec3(0, 1, 0)
	local cube = Scene.FindEntityByName("Falling Cube")
	cube.RigidBody:AddForce(vec3(0, 0, 0))
	cube.RigidBody:AddTorque(vec3(0, 0, 0))
	cube.RigidBody.LinearVelocity = vec3(0, -1, 0)
	self.CubeStartY = cube.Transform.Translation.y
end

function FeatureTest:OnFixedUpdate(dt)
	self.FixedUpdates = self.FixedUpdates + 1
end

function FeatureTest:OnUpdate(dt)
	self.Frames = self.Frames + 1
	UI.Text("Running feature test... " .. self.Frames, 20, 20, { Size = 24 })
	if self.Frames < self.SimulationFrames then
		return
	end

	local cube = Scene.FindEntityByName("Falling Cube")
	Check(cube.RigidBody:HasBody(), "RigidBody has a physics body")
	Check(cube.Transform.Translation.y < self.CubeStartY - 1.0, "dynamic body fell under gravity")
	Check(cube.Transform.Translation.y > 0.3, "dynamic body rests on the static floor")
	Check(cube.RigidBody.LinearVelocity:Length() < 1.0, "dynamic body came to rest")
	Check(self.FixedUpdates > 0, "OnFixedUpdate called")
	Check(Time.GetFrameCount() > 0 and Time.GetElapsed() > 0, "time advances")

	local collision, trigger = false, false
	for _, event in ipairs(FeatureTestEvents) do
		if event.Label == "cube" and event.Kind == "CollisionBegin" and event.Other == "Floor" then
			collision = true
		end
		if event.Label == "zone" and event.Kind == "TriggerBegin" and event.Other == "Falling Ball" then
			trigger = true
		end
	end
	Check(collision, "OnCollisionBegin received for cube vs floor")
	Check(trigger, "OnTriggerBegin received for the ball entering the trigger zone")

	local probe = Scene.FindEntityByName("Falling Cube"):GetScript()
	Check(probe ~= nil and probe.FixedUpdates > 0, "OnFixedUpdate called on other scripts")

	if Failures == 0 then
		Log.Info("FeatureTest PASSED (" .. Checks .. " checks)")
	else
		Log.Error("FeatureTest FAILED: " .. Failures .. " of " .. Checks .. " checks failed")
	end
	Application.Quit()
	self.SimulationFrames = math.huge
end

return FeatureTest
