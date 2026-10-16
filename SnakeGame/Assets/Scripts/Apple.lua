-- Makes the apple spin and bob in place. The GameManager moves it between cells.

local Apple = {
	Properties = {
		SpinSpeed = 1.5,  -- radians per second
		BobHeight = 0.08,
		BobSpeed = 3.0,
		BaseHeight = 0.45,
	},
}

function Apple:OnCreate()
	self.Time = 0
end

function Apple:OnUpdate(dt)
	self.Time = self.Time + dt
	local transform = self.Entity.Transform
	local position = transform.Translation
	transform.Translation = vec3(position.x, self.BaseHeight + math.sin(self.Time * self.BobSpeed) * self.BobHeight, position.z)
	transform.Rotation = vec3(0, self.Time * self.SpinSpeed, 0)
end

return Apple
