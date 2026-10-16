-- Used by FeatureTest.lua to verify OnCreate/OnDestroy and script properties.

local DestroyProbe = {
	Properties = {
		Value = 1,
		Name = "default",
		Offset = vec3(1, 2, 3),
	},
}

function DestroyProbe:OnCreate()
	FeatureTestLifecycle = FeatureTestLifecycle or {}
	FeatureTestLifecycle[#FeatureTestLifecycle + 1] = "create:" .. self.Name
end

function DestroyProbe:OnDestroy()
	FeatureTestLifecycle = FeatureTestLifecycle or {}
	FeatureTestLifecycle[#FeatureTestLifecycle + 1] = "destroy:" .. self.Name
end

function DestroyProbe:Double()
	return self.Value * 2
end

return DestroyProbe
