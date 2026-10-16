#include "obpch.h"
#include "Ouroboros/Scene/SceneSerializer.h"

#include "Ouroboros/Core/FileSystem.h"
#include "Ouroboros/Scene/ComponentRegistry.h"
#include "Ouroboros/Scene/Entity.h"

namespace Ouroboros {

	using json = nlohmann::json;

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	json SceneSerializer::SerializeEntity(Entity entity)
	{
		json components = json::object();
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			if (info.Has(entity))
				components[info.Name] = info.Serialize(entity);
		}

		return json{
			{ "UUID", static_cast<uint64_t>(entity.GetUUID()) },
			{ "Components", std::move(components) }
		};
	}

	Entity SceneSerializer::DeserializeEntity(Scene& scene, const json& data, bool keepUUID)
	{
		if (!data.is_object())
			throw json::type_error::create(302, "entity must be a JSON object", &data);

		UUID uuid;
		if (keepUUID)
		{
			if (auto it = data.find("UUID"); it != data.end())
			{
				const uint64_t value = it->get<uint64_t>();
				if (value != 0 && !scene.HasEntity(value))
					uuid = value;
			}
		}

		std::string name = "Entity";
		const json* components = nullptr;
		if (auto it = data.find("Components"); it != data.end())
		{
			if (!it->is_object())
				throw json::type_error::create(302, "entity Components must be a JSON object", &data);
			components = &*it;
			if (auto tag = it->find("Tag"); tag != it->end() && tag->contains("Tag"))
				name = (*tag)["Tag"].get<std::string>();
		}

		Entity entity = scene.CreateEntityWithUUID(uuid, name);
		if (!components)
			return entity;

		try
		{
			for (auto it = components->begin(); it != components->end(); ++it)
			{
				const ComponentInfo* info = ComponentRegistry::Find(it.key());
				if (!info)
				{
					OB_CORE_WARN("Ignoring unknown component '{}' on entity '{}'", it.key(), name);
					continue;
				}
				info->Deserialize(entity, it.value());
			}
		}
		catch (...)
		{
			scene.DestroyEntity(entity);
			throw;
		}

		return entity;
	}

	json SceneSerializer::Serialize() const
	{
		json entities = json::array();
		for (UUID uuid : m_Scene->GetEntityOrder())
			entities.push_back(SerializeEntity(m_Scene->GetEntityByUUID(uuid)));

		return json{
			{ "Ouroboros", "Scene" },
			{ "Version", FormatVersion },
			{ "Name", m_Scene->GetName() },
			{ "Entities", std::move(entities) }
		};
	}

	bool SceneSerializer::Deserialize(const json& data, std::string* error)
	{
		// Start from a clean slate.
		std::vector<UUID> existing = m_Scene->GetEntityOrder();
		for (UUID uuid : existing)
			m_Scene->DestroyEntity(m_Scene->GetEntityByUUID(uuid));

		try
		{
			if (!data.is_object() || data.value("Ouroboros", std::string()) != "Scene")
				throw json::other_error::create(501, "not an Ouroboros scene file", &data);

			const int version = data.value("Version", 0);
			if (version < 1 || version > FormatVersion)
				throw json::other_error::create(501, "unsupported scene version " + std::to_string(version), &data);

			m_Scene->SetName(data.value("Name", std::string("Untitled")));

			if (auto entities = data.find("Entities"); entities != data.end())
			{
				if (!entities->is_array())
					throw json::type_error::create(302, "Entities must be an array", &data);
				for (const json& entityData : *entities)
					DeserializeEntity(*m_Scene, entityData, true);
			}
		}
		catch (const json::exception& exception)
		{
			std::vector<UUID> created = m_Scene->GetEntityOrder();
			for (UUID uuid : created)
				m_Scene->DestroyEntity(m_Scene->GetEntityByUUID(uuid));

			if (error)
				*error = exception.what();
			return false;
		}

		return true;
	}

	bool SceneSerializer::SaveToFile(const std::filesystem::path& path) const
	{
		const std::string text = Serialize().dump(1, '\t');
		if (!FileSystem::WriteTextFile(path, text + "\n"))
		{
			OB_CORE_ERROR("Failed to write scene file '{}'", path.string());
			return false;
		}
		OB_CORE_INFO("Saved scene '{}' to '{}'", m_Scene->GetName(), path.string());
		return true;
	}

	bool SceneSerializer::LoadFromFile(const std::filesystem::path& path, std::string* error)
	{
		std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			if (error)
				*error = "could not read file '" + path.string() + "'";
			return false;
		}

		json data = json::parse(*text, nullptr, false);
		if (data.is_discarded())
		{
			if (error)
				*error = "invalid JSON in '" + path.string() + "'";
			return false;
		}

		std::string deserializeError;
		if (!Deserialize(data, &deserializeError))
		{
			if (error)
				*error = path.string() + ": " + deserializeError;
			return false;
		}
		return true;
	}

}
