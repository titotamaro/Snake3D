#include "obpch.h"
#include "Ouroboros/Scene/ComponentRegistry.h"

#include "Ouroboros/Scene/ComponentSerialization.h"

namespace Ouroboros {

	namespace {

		template<typename T>
		ComponentInfo MakeComponentInfo(const char* name, bool removable = true)
		{
			ComponentInfo info;
			info.Name = name;
			info.Removable = removable;
			info.Has = [](Entity entity) { return entity.HasComponent<T>(); };
			info.Add = [](Entity entity)
			{
				if (!entity.HasComponent<T>())
					entity.AddComponent<T>();
			};
			info.Remove = [](Entity entity)
			{
				if (entity.HasComponent<T>())
					entity.RemoveComponent<T>();
			};
			info.Serialize = [](Entity entity) { return nlohmann::json(entity.GetComponent<T>()); };
			info.Deserialize = [](Entity entity, const nlohmann::json& data)
			{
				// Patch a copy first so a failed patch leaves the component untouched.
				T component = entity.HasComponent<T>() ? entity.GetComponent<T>() : T();
				from_json(data, component);
				entity.AddOrReplaceComponent<T>(std::move(component));
			};
			info.Copy = [](Entity source, Entity destination)
			{
				if (source.HasComponent<T>())
					destination.AddOrReplaceComponent<T>(source.GetComponent<T>());
			};
			return info;
		}

		std::vector<ComponentInfo> CreateRegistry()
		{
			std::vector<ComponentInfo> registry;
			registry.push_back(MakeComponentInfo<TagComponent>("Tag", false));
			registry.push_back(MakeComponentInfo<TransformComponent>("Transform", false));
			registry.push_back(MakeComponentInfo<CameraComponent>("Camera"));
			registry.push_back(MakeComponentInfo<MeshRendererComponent>("MeshRenderer"));
			registry.push_back(MakeComponentInfo<DirectionalLightComponent>("DirectionalLight"));
			registry.push_back(MakeComponentInfo<PointLightComponent>("PointLight"));
			registry.push_back(MakeComponentInfo<SkyLightComponent>("SkyLight"));
			registry.push_back(MakeComponentInfo<RigidBodyComponent>("RigidBody"));
			registry.push_back(MakeComponentInfo<BoxColliderComponent>("BoxCollider"));
			registry.push_back(MakeComponentInfo<SphereColliderComponent>("SphereCollider"));
			registry.push_back(MakeComponentInfo<ScriptComponent>("Script"));
			registry.push_back(MakeComponentInfo<AudioSourceComponent>("AudioSource"));
			return registry;
		}

	}

	const std::vector<ComponentInfo>& ComponentRegistry::GetAll()
	{
		static const std::vector<ComponentInfo> s_Registry = CreateRegistry();
		return s_Registry;
	}

	const ComponentInfo* ComponentRegistry::Find(std::string_view name)
	{
		for (const ComponentInfo& info : GetAll())
		{
			if (info.Name == name)
				return &info;
		}
		return nullptr;
	}

}
