-- Unit tests for SnakeLogic.lua. Returns a function that runs them and returns (failureCount, messages).
-- Executed by OuroborosTests (ScriptingTests.cpp).

local SnakeLogic = require("SnakeLogic")
local D = SnakeLogic.Directions

local function FixedRandom(values)
	local index = 0
	return function(minimum, maximum)
		index = index + 1
		local value = values[((index - 1) % #values) + 1]
		return math.max(minimum, math.min(maximum, value))
	end
end

local Tests = {}

Tests["new game places a straight snake and an apple"] = function(check)
	local state = SnakeLogic.NewGame(10, 3, FixedRandom({ 1 }))
	check(#state.Body == 3, "length 3")
	check(state.Body[1].x == 5 and state.Body[1].y == 5, "head at the center")
	check(state.Body[3].x == 3, "body extends to the left")
	check(state.Apple ~= nil and not SnakeLogic.IsOccupied(state, state.Apple), "apple on a free cell")
	check(state.Alive and state.Score == 0, "alive with zero score")
end

Tests["moving advances the head and keeps the length"] = function(check)
	local state = SnakeLogic.NewGame(10, 3, FixedRandom({ 1 }))
	state.Apple = { x = 0, y = 0 }
	check(SnakeLogic.Step(state) == "move", "move event")
	check(state.Body[1].x == 6 and #state.Body == 3, "moved right")
end

Tests["turning is queued and reversing is rejected"] = function(check)
	local state = SnakeLogic.NewGame(10, 3, FixedRandom({ 1 }))
	state.Apple = { x = 0, y = 0 }
	check(not SnakeLogic.QueueDirection(state, D.Left), "cannot reverse")
	check(not SnakeLogic.QueueDirection(state, D.Right), "same direction ignored")
	check(SnakeLogic.QueueDirection(state, D.Up), "turn up accepted")
	check(not SnakeLogic.QueueDirection(state, D.Down), "cannot reverse the queued turn")
	check(SnakeLogic.QueueDirection(state, D.Left), "second queued turn")
	check(not SnakeLogic.QueueDirection(state, D.Down), "queue holds two turns")
	SnakeLogic.Step(state)
	check(state.Body[1].y == 4, "first turn applied")
	SnakeLogic.Step(state)
	check(state.Body[1].x == 4, "second turn applied")
end

Tests["eating grows the snake and respawns the apple"] = function(check)
	local state = SnakeLogic.NewGame(10, 3, FixedRandom({ 1 }))
	state.Apple = { x = 6, y = 5 }
	check(SnakeLogic.Step(state) == "eat", "eat event")
	check(#state.Body == 4 and state.Score == 1 and state.Grew, "grew by one")
	check(state.Apple ~= nil and not SnakeLogic.IsOccupied(state, state.Apple), "new apple on a free cell")
end

Tests["hitting a wall ends the game"] = function(check)
	local state = SnakeLogic.NewGame(6, 2, FixedRandom({ 1 }))
	state.Apple = { x = 0, y = 0 }
	local event
	for _ = 1, 10 do
		event = SnakeLogic.Step(state)
		if event == "dead" then
			break
		end
	end
	check(event == "dead" and not state.Alive, "died at the wall")
	check(SnakeLogic.Step(state) == "dead", "stays dead")
end

Tests["running into the body ends the game"] = function(check)
	local state = SnakeLogic.NewGame(10, 5, FixedRandom({ 1 }))
	state.Apple = { x = 0, y = 0 }
	SnakeLogic.QueueDirection(state, D.Up)
	SnakeLogic.Step(state)
	SnakeLogic.QueueDirection(state, D.Left)
	SnakeLogic.Step(state)
	SnakeLogic.QueueDirection(state, D.Down)
	check(SnakeLogic.Step(state) == "dead", "bit its own body")
end

Tests["moving into the tail cell that is vacated is allowed"] = function(check)
	local state = SnakeLogic.NewGame(10, 4, FixedRandom({ 1 }))
	state.Apple = { x = 0, y = 0 }
	-- Build a 2x2 loop: the head moves into the cell the tail leaves this step.
	state.Body = { { x = 5, y = 5 }, { x = 5, y = 6 }, { x = 4, y = 6 }, { x = 4, y = 5 } }
	state.Direction = D.Up
	state.PendingDirections = {}
	SnakeLogic.QueueDirection(state, D.Left)
	check(SnakeLogic.Step(state) == "move", "followed its tail")
end

Tests["filling the board wins"] = function(check)
	local state = SnakeLogic.NewGame(4, 2, FixedRandom({ 1 }))
	-- The snake occupies every cell but (3,0); its head is at (2,0) and the apple is on the last free cell.
	state.Body = { { x = 2, y = 0 } }
	for y = 0, 3 do
		for x = 0, 3 do
			if not (y == 0 and (x == 2 or x == 3)) then
				state.Body[#state.Body + 1] = { x = x, y = y }
			end
		end
	end
	state.Direction = D.Right
	state.PendingDirections = {}
	state.Apple = { x = 3, y = 0 }
	check(SnakeLogic.Step(state) == "win", "win event")
	check(SnakeLogic.SpawnApple(state) == nil, "no free cell left")
end

Tests["CopyBody makes an independent copy"] = function(check)
	local state = SnakeLogic.NewGame(10, 3, FixedRandom({ 1 }))
	local copy = SnakeLogic.CopyBody(state)
	copy[1].x = 100
	check(state.Body[1].x ~= 100, "copy is independent")
end

return function()
	local failures = 0
	local messages = {}
	local names = {}
	for name in pairs(Tests) do
		names[#names + 1] = name
	end
	table.sort(names)
	for _, name in ipairs(names) do
		local function check(condition, message)
			if not condition then
				failures = failures + 1
				messages[#messages + 1] = name .. ": " .. message
			end
		end
		local ok, error = pcall(Tests[name], check)
		if not ok then
			failures = failures + 1
			messages[#messages + 1] = name .. ": " .. tostring(error)
		end
	end
	return failures, table.concat(messages, "\n"), #names
end
