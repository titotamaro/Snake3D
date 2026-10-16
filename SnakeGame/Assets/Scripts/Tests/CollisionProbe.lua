-- Records physics callbacks into the global FeatureTestEvents table (read by FeatureTest.lua).

local CollisionProbe = {
	Properties = {
		Label = "probe",
	},
}

local function Record(self, kind, other)
	FeatureTestEvents = FeatureTestEvents or {}
	FeatureTestEvents[#FeatureTestEvents + 1] = { Label = self.Label, Kind = kind, Other = other.Name }
end

function CollisionProbe:OnCreate()
	self.FixedUpdates = 0
end

function CollisionProbe:OnFixedUpdate(dt)
	self.FixedUpdates = self.FixedUpdates + 1
end

function CollisionProbe:OnCollisionBegin(other)
	Record(self, "CollisionBegin", other)
end

function CollisionProbe:OnCollisionEnd(other)
	Record(self, "CollisionEnd", other)
end

function CollisionProbe:OnTriggerBegin(other)
	Record(self, "TriggerBegin", other)
end

function CollisionProbe:OnTriggerEnd(other)
	Record(self, "TriggerEnd", other)
end

return CollisionProbe
