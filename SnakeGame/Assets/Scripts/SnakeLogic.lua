-- Pure Snake game rules (no engine calls), so they can be unit tested in isolation.
-- Cells are {x = column, y = row} with 0 <= x, y < size.

local SnakeLogic = {}

SnakeLogic.Directions = {
	Up = { x = 0, y = -1 },
	Down = { x = 0, y = 1 },
	Left = { x = -1, y = 0 },
	Right = { x = 1, y = 0 },
}

local function CellKey(cell)
	return cell.x .. ":" .. cell.y
end

local function IsOpposite(a, b)
	return a.x == -b.x and a.y == -b.y
end

local function Copy(cell)
	return { x = cell.x, y = cell.y }
end

-- Creates a new game. `random(min, max)` must return an integer in [min, max] (inclusive).
function SnakeLogic.NewGame(size, initialLength, random)
	assert(size >= 4, "board too small")
	initialLength = math.max(1, math.min(initialLength or 3, size // 2))

	local state = {
		Size = size,
		Body = {},              -- Body[1] is the head
		Direction = SnakeLogic.Directions.Right,
		PendingDirections = {}, -- queued turns (at most 2), applied one per step
		Score = 0,
		Alive = true,
		Grew = false,           -- true during the step in which an apple was eaten
		Random = random,
	}

	local headX = size // 2
	local row = size // 2
	for i = 0, initialLength - 1 do
		state.Body[#state.Body + 1] = { x = headX - i, y = row }
	end

	state.Apple = SnakeLogic.SpawnApple(state)
	return state
end

function SnakeLogic.IsOccupied(state, cell)
	for _, segment in ipairs(state.Body) do
		if segment.x == cell.x and segment.y == cell.y then
			return true
		end
	end
	return false
end

-- Picks a random free cell for the apple, or nil if the board is full.
function SnakeLogic.SpawnApple(state)
	local occupied = {}
	for _, segment in ipairs(state.Body) do
		occupied[CellKey(segment)] = true
	end

	local free = {}
	for y = 0, state.Size - 1 do
		for x = 0, state.Size - 1 do
			if not occupied[x .. ":" .. y] then
				free[#free + 1] = { x = x, y = y }
			end
		end
	end

	if #free == 0 then
		return nil
	end
	return free[state.Random(1, #free)]
end

-- Queues a turn. Reversing into the snake is ignored. Returns true if the turn was accepted.
function SnakeLogic.QueueDirection(state, direction)
	if #state.PendingDirections >= 2 then
		return false
	end

	local last = state.PendingDirections[#state.PendingDirections] or state.Direction
	if (last.x == direction.x and last.y == direction.y) or IsOpposite(last, direction) then
		return false
	end

	state.PendingDirections[#state.PendingDirections + 1] = direction
	return true
end

-- Advances the game by one move. Returns an event: "move", "eat", "win" or "dead".
function SnakeLogic.Step(state)
	if not state.Alive then
		return "dead"
	end

	if #state.PendingDirections > 0 then
		state.Direction = table.remove(state.PendingDirections, 1)
	end

	local head = state.Body[1]
	local nextHead = { x = head.x + state.Direction.x, y = head.y + state.Direction.y }
	state.Grew = false

	-- Walls
	if nextHead.x < 0 or nextHead.y < 0 or nextHead.x >= state.Size or nextHead.y >= state.Size then
		state.Alive = false
		return "dead"
	end

	local eats = state.Apple ~= nil and nextHead.x == state.Apple.x and nextHead.y == state.Apple.y

	-- Self collision. The tail cell is vacated this step unless the snake grows.
	local checkedLength = eats and #state.Body or #state.Body - 1
	for i = 1, checkedLength do
		local segment = state.Body[i]
		if segment.x == nextHead.x and segment.y == nextHead.y then
			state.Alive = false
			return "dead"
		end
	end

	table.insert(state.Body, 1, nextHead)
	if eats then
		state.Score = state.Score + 1
		state.Grew = true
		state.Apple = SnakeLogic.SpawnApple(state)
		if state.Apple == nil then
			return "win"
		end
		return "eat"
	end

	table.remove(state.Body)
	return "move"
end

-- Snapshot of the body positions (used to interpolate rendering between steps).
function SnakeLogic.CopyBody(state)
	local copy = {}
	for i, segment in ipairs(state.Body) do
		copy[i] = Copy(segment)
	end
	return copy
end

return SnakeLogic
