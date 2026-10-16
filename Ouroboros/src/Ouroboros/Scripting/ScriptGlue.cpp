#include "obpch.h"
#include "Ouroboros/Scripting/ScriptGlue.h"

#include "Ouroboros/Audio/AudioEngine.h"
#include "Ouroboros/Audio/SceneAudio.h"
#include "Ouroboros/Core/Input.h"
#include "Ouroboros/Physics/PhysicsWorld.h"
#include "Ouroboros/Project/Project.h"
#include "Ouroboros/Scene/ComponentRegistry.h"
#include "Ouroboros/Scene/ComponentSerialization.h"
#include "Ouroboros/Scene/Entity.h"
#include "Ouroboros/Scripting/ScriptRuntime.h"

#include <glm/gtc/quaternion.hpp>

namespace Ouroboros {

	using json = nlohmann::json;

	// ------------------------------------------------------------------------------------------------------------
	// LuaEntity

	Entity LuaEntity::Resolve() const
	{
		if (!SceneContext)
			return {};
		return SceneContext->GetEntityByUUID(ID);
	}

	Entity LuaEntity::Get() const
	{
		Entity entity = Resolve();
		if (!entity)
			throw std::runtime_error("entity " + std::to_string(static_cast<uint64_t>(ID)) + " no longer exists");
		return entity;
	}

	namespace {

		// --------------------------------------------------------------------------------------------------------
		// Component proxies: resolve the component on every access so scripts never hold dangling references.

		template<typename T>
		struct ComponentName;

		template<> struct ComponentName<TransformComponent> { static constexpr const char* Value = "Transform"; };
		template<> struct ComponentName<CameraComponent> { static constexpr const char* Value = "Camera"; };
		template<> struct ComponentName<MeshRendererComponent> { static constexpr const char* Value = "MeshRenderer"; };
		template<> struct ComponentName<DirectionalLightComponent> { static constexpr const char* Value = "DirectionalLight"; };
		template<> struct ComponentName<PointLightComponent> { static constexpr const char* Value = "PointLight"; };
		template<> struct ComponentName<SkyLightComponent> { static constexpr const char* Value = "SkyLight"; };
		template<> struct ComponentName<RigidBodyComponent> { static constexpr const char* Value = "RigidBody"; };
		template<> struct ComponentName<BoxColliderComponent> { static constexpr const char* Value = "BoxCollider"; };
		template<> struct ComponentName<SphereColliderComponent> { static constexpr const char* Value = "SphereCollider"; };
		template<> struct ComponentName<AudioSourceComponent> { static constexpr const char* Value = "AudioSource"; };

		template<typename T>
		struct ComponentProxy
		{
			LuaEntity Owner;

			T& Get() const
			{
				Entity entity = Owner.Get();
				T* component = entity.TryGetComponent<T>();
				if (!component)
					throw std::runtime_error("entity '" + entity.GetName() + "' has no " + ComponentName<T>::Value + " component");
				return *component;
			}
		};

		using TransformProxy = ComponentProxy<TransformComponent>;
		using CameraProxy = ComponentProxy<CameraComponent>;
		using MeshRendererProxy = ComponentProxy<MeshRendererComponent>;
		using DirectionalLightProxy = ComponentProxy<DirectionalLightComponent>;
		using PointLightProxy = ComponentProxy<PointLightComponent>;
		using SkyLightProxy = ComponentProxy<SkyLightComponent>;
		using RigidBodyProxy = ComponentProxy<RigidBodyComponent>;
		using BoxColliderProxy = ComponentProxy<BoxColliderComponent>;
		using SphereColliderProxy = ComponentProxy<SphereColliderComponent>;
		using AudioSourceProxy = ComponentProxy<AudioSourceComponent>;

#define OB_PROXY_FIELD(ComponentType, Field)                                                                       \
	#Field, sol::property(                                                                                         \
		[](const ComponentProxy<ComponentType>& proxy) { return proxy.Get().Field; },                             \
		[](const ComponentProxy<ComponentType>& proxy, decltype(ComponentType::Field) value) { proxy.Get().Field = value; })

		template<typename T>
		sol::object GetProxy(const LuaEntity& owner, sol::this_state state)
		{
			// Accessing a component the entity does not have yields nil, so scripts can test `if e.RigidBody then`.
			Entity entity = owner.Get();
			if (!entity.HasComponent<T>())
				return sol::make_object(state, sol::lua_nil);
			return sol::make_object(state, ComponentProxy<T>{ owner });
		}

		// --------------------------------------------------------------------------------------------------------
		// Helpers

		const ComponentInfo& FindComponentOrThrow(const std::string& name)
		{
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!info)
				throw std::runtime_error("unknown component '" + name + "'");
			return *info;
		}

		KeyCode ToKeyCode(const sol::object& key)
		{
			if (key.get_type() == sol::type::number)
				return static_cast<KeyCode>(key.as<int>());
			if (key.get_type() == sol::type::string)
			{
				const KeyCode code = Input::KeyFromName(key.as<std::string>());
				if (code == 0)
					throw std::runtime_error("unknown key '" + key.as<std::string>() + "'");
				return code;
			}
			throw std::runtime_error("key must be a Key.* constant or a key name");
		}

		std::string ToString(sol::this_state state, const sol::variadic_args& args)
		{
			sol::state_view lua(state);
			sol::protected_function toString = lua["tostring"];
			std::string text;
			for (size_t i = 0; i < args.size(); i++)
			{
				if (i > 0)
					text += "\t";
				sol::protected_function_result result = toString(args[i]);
				text += result.valid() ? result.get<std::string>() : std::string("?");
			}
			return text;
		}

		std::optional<UIAnchor> AnchorFromString(std::string_view name)
		{
			static constexpr std::pair<std::string_view, UIAnchor> anchors[] = {
				{ "TopLeft", UIAnchor::TopLeft }, { "Top", UIAnchor::Top }, { "TopRight", UIAnchor::TopRight },
				{ "Left", UIAnchor::Left }, { "Center", UIAnchor::Center }, { "Right", UIAnchor::Right },
				{ "BottomLeft", UIAnchor::BottomLeft }, { "Bottom", UIAnchor::Bottom }, { "BottomRight", UIAnchor::BottomRight }
			};
			for (const auto& [anchorName, anchor] : anchors)
			{
				if (anchorName == name)
					return anchor;
			}
			return std::nullopt;
		}

		// --------------------------------------------------------------------------------------------------------
		// Registration

		void RegisterMath(sol::state& lua)
		{
			lua.new_usertype<glm::vec2>("vec2",
				sol::call_constructor, sol::factories(
					[]() { return glm::vec2(0.0f); },
					[](float scalar) { return glm::vec2(scalar); },
					[](float x, float y) { return glm::vec2(x, y); }),
				"x", &glm::vec2::x,
				"y", &glm::vec2::y,
				sol::meta_function::addition, [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
				sol::meta_function::subtraction, [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::vec2& a, const glm::vec2& b) { return a * b; },
					[](const glm::vec2& a, float s) { return a * s; },
					[](float s, const glm::vec2& a) { return a * s; }),
				sol::meta_function::division, sol::overload(
					[](const glm::vec2& a, const glm::vec2& b) { return a / b; },
					[](const glm::vec2& a, float s) { return a / s; }),
				sol::meta_function::unary_minus, [](const glm::vec2& a) { return -a; },
				sol::meta_function::equal_to, [](const glm::vec2& a, const glm::vec2& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::vec2& v) { return fmt::format("vec2({}, {})", v.x, v.y); },
				"Length", [](const glm::vec2& v) { return glm::length(v); },
				"Normalized", [](const glm::vec2& v) { const float l = glm::length(v); return l > 0.0f ? v / l : glm::vec2(0.0f); },
				"Dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); },
				"Distance", [](const glm::vec2& a, const glm::vec2& b) { return glm::distance(a, b); },
				"Lerp", [](const glm::vec2& a, const glm::vec2& b, float t) { return glm::mix(a, b, t); });

			lua.new_usertype<glm::vec3>("vec3",
				sol::call_constructor, sol::factories(
					[]() { return glm::vec3(0.0f); },
					[](float scalar) { return glm::vec3(scalar); },
					[](float x, float y, float z) { return glm::vec3(x, y, z); }),
				"x", &glm::vec3::x,
				"y", &glm::vec3::y,
				"z", &glm::vec3::z,
				sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
				sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::vec3& a, const glm::vec3& b) { return a * b; },
					[](const glm::vec3& a, float s) { return a * s; },
					[](float s, const glm::vec3& a) { return a * s; }),
				sol::meta_function::division, sol::overload(
					[](const glm::vec3& a, const glm::vec3& b) { return a / b; },
					[](const glm::vec3& a, float s) { return a / s; }),
				sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
				sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::vec3& v) { return fmt::format("vec3({}, {}, {})", v.x, v.y, v.z); },
				"Length", [](const glm::vec3& v) { return glm::length(v); },
				"Normalized", [](const glm::vec3& v) { const float l = glm::length(v); return l > 0.0f ? v / l : glm::vec3(0.0f); },
				"Dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
				"Cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
				"Distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
				"Lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });

			lua.new_usertype<glm::vec4>("vec4",
				sol::call_constructor, sol::factories(
					[]() { return glm::vec4(0.0f); },
					[](float scalar) { return glm::vec4(scalar); },
					[](float x, float y, float z, float w) { return glm::vec4(x, y, z, w); }),
				"x", &glm::vec4::x,
				"y", &glm::vec4::y,
				"z", &glm::vec4::z,
				"w", &glm::vec4::w,
				sol::meta_function::addition, [](const glm::vec4& a, const glm::vec4& b) { return a + b; },
				sol::meta_function::subtraction, [](const glm::vec4& a, const glm::vec4& b) { return a - b; },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::vec4& a, const glm::vec4& b) { return a * b; },
					[](const glm::vec4& a, float s) { return a * s; },
					[](float s, const glm::vec4& a) { return a * s; }),
				sol::meta_function::division, sol::overload(
					[](const glm::vec4& a, const glm::vec4& b) { return a / b; },
					[](const glm::vec4& a, float s) { return a / s; }),
				sol::meta_function::unary_minus, [](const glm::vec4& a) { return -a; },
				sol::meta_function::equal_to, [](const glm::vec4& a, const glm::vec4& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::vec4& v) { return fmt::format("vec4({}, {}, {}, {})", v.x, v.y, v.z, v.w); },
				"Length", [](const glm::vec4& v) { return glm::length(v); },
				"Dot", [](const glm::vec4& a, const glm::vec4& b) { return glm::dot(a, b); },
				"Lerp", [](const glm::vec4& a, const glm::vec4& b, float t) { return glm::mix(a, b, t); });

			lua.new_usertype<glm::quat>("quat",
				sol::call_constructor, sol::factories(
					[]() { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); },
					[](float w, float x, float y, float z) { return glm::quat(w, x, y, z); }),
				"x", &glm::quat::x,
				"y", &glm::quat::y,
				"z", &glm::quat::z,
				"w", &glm::quat::w,
				"FromEuler", [](const glm::vec3& euler) { return glm::quat(euler); },
				"AngleAxis", [](float angle, const glm::vec3& axis) { return glm::angleAxis(angle, glm::normalize(axis)); },
				"ToEuler", [](const glm::quat& q) { return glm::eulerAngles(q); },
				"Rotate", [](const glm::quat& q, const glm::vec3& v) { return q * v; },
				"Normalized", [](const glm::quat& q) { return glm::normalize(q); },
				"Slerp", [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); },
				sol::meta_function::multiplication, sol::overload(
					[](const glm::quat& a, const glm::quat& b) { return a * b; },
					[](const glm::quat& q, const glm::vec3& v) { return q * v; }),
				sol::meta_function::equal_to, [](const glm::quat& a, const glm::quat& b) { return a == b; },
				sol::meta_function::to_string, [](const glm::quat& q) { return fmt::format("quat({}, {}, {}, {})", q.w, q.x, q.y, q.z); });

			sol::table math = lua.create_named_table("Math");
			math["Pi"] = Math::Pi;
			math["Lerp"] = [](float a, float b, float t) { return a + (b - a) * t; };
			math["Clamp"] = [](float value, float minimum, float maximum) { return std::clamp(value, minimum, maximum); };
			math["Radians"] = [](float degrees) { return glm::radians(degrees); };
			math["Degrees"] = [](float radians) { return glm::degrees(radians); };
			math["Sign"] = [](float value) { return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f); };
			math["MoveTowards"] = [](float current, float target, float maxDelta)
			{
				if (std::abs(target - current) <= maxDelta)
					return target;
				return current + (target > current ? maxDelta : -maxDelta);
			};
		}

		void RegisterCore(sol::state& lua, ScriptRuntime& runtime)
		{
			sol::table engine = lua.create_named_table("Ouroboros");
			engine["Version"] = OB_VERSION_STRING;

			sol::table log = lua.create_named_table("Log");
			log["Trace"] = [](sol::this_state state, sol::variadic_args args) { OB_TRACE("{}", ToString(state, args)); };
			log["Info"] = [](sol::this_state state, sol::variadic_args args) { OB_INFO("{}", ToString(state, args)); };
			log["Warn"] = [](sol::this_state state, sol::variadic_args args) { OB_WARN("{}", ToString(state, args)); };
			log["Error"] = [](sol::this_state state, sol::variadic_args args) { OB_ERROR("{}", ToString(state, args)); };
			lua["print"] = [](sol::this_state state, sol::variadic_args args) { OB_INFO("{}", ToString(state, args)); };

			sol::table time = lua.create_named_table("Time");
			time["GetDeltaTime"] = [&runtime]() { return runtime.GetDeltaTime(); };
			time["GetElapsed"] = [&runtime]() { return runtime.GetScene()->GetTime(); };
			time["GetFrameCount"] = [&runtime]() { return static_cast<int64_t>(runtime.GetScene()->GetFrameIndex()); };
			time["GetTimeScale"] = [&runtime]() { return runtime.GetScene()->GetTimeScale(); };
			time["SetTimeScale"] = [&runtime](float scale) { runtime.GetScene()->SetTimeScale(scale); };
			time["GetFixedDeltaTime"] = []() { return Scene::FixedTimestep; };

			sol::table random = lua.create_named_table("Random");
			random["Seed"] = [&runtime](int64_t seed) { runtime.GetRandomEngine().seed(static_cast<uint32_t>(seed)); };
			random["Int"] = [&runtime](int64_t minimum, int64_t maximum)
			{
				if (maximum < minimum)
					throw std::runtime_error("Random.Int: max must be >= min");
				std::uniform_int_distribution<int64_t> distribution(minimum, maximum);
				return distribution(runtime.GetRandomEngine());
			};
			random["Float"] = sol::overload(
				[&runtime]()
				{
					std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
					return distribution(runtime.GetRandomEngine());
				},
				[&runtime](float minimum, float maximum)
				{
					std::uniform_real_distribution<float> distribution(std::min(minimum, maximum), std::max(minimum, maximum));
					return distribution(runtime.GetRandomEngine());
				});

			sol::table application = lua.create_named_table("Application");
			application["Quit"] = [&runtime]() { runtime.GetScene()->RequestQuit(); };
			application["GetViewportSize"] = [&runtime]()
			{
				return glm::vec2(static_cast<float>(runtime.GetScene()->GetViewportWidth()), static_cast<float>(runtime.GetScene()->GetViewportHeight()));
			};
		}

		void RegisterInput(sol::state& lua)
		{
			sol::table keys = lua.create_named_table("Key");
			for (KeyCode code = 0; code < Key::MaxKeyCode; code++)
			{
				const char* name = Input::KeyToName(code);
				if (std::strcmp(name, "Unknown") == 0)
					continue;
				// Digit keys are exposed as Key.D0..Key.D9 (identifiers cannot start with a digit).
				const std::string keyName = std::isdigit(static_cast<unsigned char>(name[0])) ? std::string("D") + name : std::string(name);
				keys[keyName] = code;
			}

			sol::table mouse = lua.create_named_table("Mouse");
			mouse["Left"] = static_cast<int>(Mouse::ButtonLeft);
			mouse["Right"] = static_cast<int>(Mouse::ButtonRight);
			mouse["Middle"] = static_cast<int>(Mouse::ButtonMiddle);

			sol::table input = lua.create_named_table("Input");
			input["IsKeyDown"] = [](const sol::object& key) { return Input::IsKeyDown(ToKeyCode(key)); };
			input["IsKeyPressed"] = [](const sol::object& key) { return Input::IsKeyPressed(ToKeyCode(key)); };
			input["IsKeyReleased"] = [](const sol::object& key) { return Input::IsKeyReleased(ToKeyCode(key)); };
			input["IsMouseButtonDown"] = [](int button) { return Input::IsMouseButtonDown(static_cast<MouseCode>(button)); };
			input["IsMouseButtonPressed"] = [](int button) { return Input::IsMouseButtonPressed(static_cast<MouseCode>(button)); };
			input["GetMousePosition"] = []() { return Input::GetMousePosition(); };
			input["GetMouseDelta"] = []() { return Input::GetMouseDelta(); };
			input["GetScrollDelta"] = []() { return Input::GetScrollDelta(); };
		}

		void RegisterComponents(sol::state& lua)
		{
			lua.new_usertype<TransformProxy>("TransformComponent", sol::no_constructor,
				OB_PROXY_FIELD(TransformComponent, Translation),
				OB_PROXY_FIELD(TransformComponent, Rotation),
				OB_PROXY_FIELD(TransformComponent, Scale),
				"GetForward", [](const TransformProxy& proxy) { return proxy.Get().GetForward(); },
				"GetRight", [](const TransformProxy& proxy) { return proxy.Get().GetRight(); },
				"GetUp", [](const TransformProxy& proxy) { return proxy.Get().GetUp(); },
				"GetRotationQuat", [](const TransformProxy& proxy) { return proxy.Get().GetRotationQuat(); },
				"SetRotationQuat", [](const TransformProxy& proxy, const glm::quat& rotation) { proxy.Get().SetRotationQuat(rotation); },
				"LookAt", [](const TransformProxy& proxy, const glm::vec3& target)
				{
					TransformComponent& transform = proxy.Get();
					const glm::vec3 direction = target - transform.Translation;
					if (glm::length(direction) < 1e-6f)
						return;
					const glm::vec3 forward = glm::normalize(direction);
					const glm::vec3 up = std::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
					transform.SetRotationQuat(glm::quatLookAtRH(forward, up));
				});

			lua.new_usertype<CameraProxy>("CameraComponent", sol::no_constructor,
				"FieldOfView", sol::property(
					[](const CameraProxy& proxy) { return glm::degrees(proxy.Get().PerspectiveFOV); },
					[](const CameraProxy& proxy, float degrees) { proxy.Get().PerspectiveFOV = glm::radians(degrees); }),
				"Projection", sol::property(
					[](const CameraProxy& proxy) { return std::string(ProjectionTypeToString(proxy.Get().Projection)); },
					[](const CameraProxy& proxy, const std::string& name)
					{
						const auto type = ProjectionTypeFromString(name);
						if (!type)
							throw std::runtime_error("unknown projection '" + name + "'");
						proxy.Get().Projection = *type;
					}),
				OB_PROXY_FIELD(CameraComponent, PerspectiveNear),
				OB_PROXY_FIELD(CameraComponent, PerspectiveFar),
				OB_PROXY_FIELD(CameraComponent, OrthographicSize),
				OB_PROXY_FIELD(CameraComponent, OrthographicNear),
				OB_PROXY_FIELD(CameraComponent, OrthographicFar),
				OB_PROXY_FIELD(CameraComponent, Exposure),
				OB_PROXY_FIELD(CameraComponent, Primary));

			lua.new_usertype<MeshRendererProxy>("MeshRendererComponent", sol::no_constructor,
				OB_PROXY_FIELD(MeshRendererComponent, Mesh),
				OB_PROXY_FIELD(MeshRendererComponent, Color),
				OB_PROXY_FIELD(MeshRendererComponent, Metallic),
				OB_PROXY_FIELD(MeshRendererComponent, Roughness),
				OB_PROXY_FIELD(MeshRendererComponent, Emissive),
				OB_PROXY_FIELD(MeshRendererComponent, EmissiveIntensity),
				OB_PROXY_FIELD(MeshRendererComponent, CastShadows),
				OB_PROXY_FIELD(MeshRendererComponent, Visible));

			lua.new_usertype<DirectionalLightProxy>("DirectionalLightComponent", sol::no_constructor,
				OB_PROXY_FIELD(DirectionalLightComponent, Color),
				OB_PROXY_FIELD(DirectionalLightComponent, Intensity),
				OB_PROXY_FIELD(DirectionalLightComponent, CastShadows),
				OB_PROXY_FIELD(DirectionalLightComponent, ShadowSoftness),
				OB_PROXY_FIELD(DirectionalLightComponent, ShadowDistance));

			lua.new_usertype<PointLightProxy>("PointLightComponent", sol::no_constructor,
				OB_PROXY_FIELD(PointLightComponent, Color),
				OB_PROXY_FIELD(PointLightComponent, Intensity),
				OB_PROXY_FIELD(PointLightComponent, Radius));

			lua.new_usertype<SkyLightProxy>("SkyLightComponent", sol::no_constructor,
				OB_PROXY_FIELD(SkyLightComponent, Environment),
				OB_PROXY_FIELD(SkyLightComponent, Intensity),
				OB_PROXY_FIELD(SkyLightComponent, Rotation),
				OB_PROXY_FIELD(SkyLightComponent, ShowBackground));

			auto physicsOf = [](const RigidBodyProxy& proxy) -> PhysicsWorld*
			{
				proxy.Get(); // validates entity and component
				return proxy.Owner.SceneContext->GetPhysicsWorld();
			};

			lua.new_usertype<RigidBodyProxy>("RigidBodyComponent", sol::no_constructor,
				"Type", sol::property(
					[](const RigidBodyProxy& proxy) { return std::string(RigidBodyTypeToString(proxy.Get().Type)); },
					[](const RigidBodyProxy& proxy, const std::string& name)
					{
						const auto type = RigidBodyTypeFromString(name);
						if (!type)
							throw std::runtime_error("unknown rigid body type '" + name + "'");
						proxy.Get().Type = *type;
					}),
				OB_PROXY_FIELD(RigidBodyComponent, Mass),
				OB_PROXY_FIELD(RigidBodyComponent, LinearDamping),
				OB_PROXY_FIELD(RigidBodyComponent, AngularDamping),
				OB_PROXY_FIELD(RigidBodyComponent, GravityFactor),
				OB_PROXY_FIELD(RigidBodyComponent, FixedRotation),
				"LinearVelocity", sol::property(
					[physicsOf](const RigidBodyProxy& proxy)
					{
						PhysicsWorld* physics = physicsOf(proxy);
						return physics ? physics->GetLinearVelocity(proxy.Owner.ID) : glm::vec3(0.0f);
					},
					[physicsOf](const RigidBodyProxy& proxy, const glm::vec3& velocity)
					{
						if (PhysicsWorld* physics = physicsOf(proxy))
							physics->SetLinearVelocity(proxy.Owner.ID, velocity);
					}),
				"AngularVelocity", sol::property(
					[physicsOf](const RigidBodyProxy& proxy)
					{
						PhysicsWorld* physics = physicsOf(proxy);
						return physics ? physics->GetAngularVelocity(proxy.Owner.ID) : glm::vec3(0.0f);
					},
					[physicsOf](const RigidBodyProxy& proxy, const glm::vec3& velocity)
					{
						if (PhysicsWorld* physics = physicsOf(proxy))
							physics->SetAngularVelocity(proxy.Owner.ID, velocity);
					}),
				"AddForce", [physicsOf](const RigidBodyProxy& proxy, const glm::vec3& force)
				{
					if (PhysicsWorld* physics = physicsOf(proxy))
						physics->AddForce(proxy.Owner.ID, force);
				},
				"AddImpulse", [physicsOf](const RigidBodyProxy& proxy, const glm::vec3& impulse)
				{
					if (PhysicsWorld* physics = physicsOf(proxy))
					{
						// Bodies for components added this frame are created on the next physics step; make sure the
						// impulse is not lost.
						if (!physics->HasBody(proxy.Owner.ID))
							physics->SyncBodies();
						physics->AddImpulse(proxy.Owner.ID, impulse);
					}
				},
				"AddTorque", [physicsOf](const RigidBodyProxy& proxy, const glm::vec3& torque)
				{
					if (PhysicsWorld* physics = physicsOf(proxy))
						physics->AddTorque(proxy.Owner.ID, torque);
				},
				"HasBody", [physicsOf](const RigidBodyProxy& proxy)
				{
					PhysicsWorld* physics = physicsOf(proxy);
					return physics && physics->HasBody(proxy.Owner.ID);
				});

			lua.new_usertype<BoxColliderProxy>("BoxColliderComponent", sol::no_constructor,
				OB_PROXY_FIELD(BoxColliderComponent, HalfExtents),
				OB_PROXY_FIELD(BoxColliderComponent, Offset),
				OB_PROXY_FIELD(BoxColliderComponent, Friction),
				OB_PROXY_FIELD(BoxColliderComponent, Restitution),
				OB_PROXY_FIELD(BoxColliderComponent, IsTrigger));

			lua.new_usertype<SphereColliderProxy>("SphereColliderComponent", sol::no_constructor,
				OB_PROXY_FIELD(SphereColliderComponent, Radius),
				OB_PROXY_FIELD(SphereColliderComponent, Offset),
				OB_PROXY_FIELD(SphereColliderComponent, Friction),
				OB_PROXY_FIELD(SphereColliderComponent, Restitution),
				OB_PROXY_FIELD(SphereColliderComponent, IsTrigger));

			lua.new_usertype<AudioSourceProxy>("AudioSourceComponent", sol::no_constructor,
				OB_PROXY_FIELD(AudioSourceComponent, Clip),
				OB_PROXY_FIELD(AudioSourceComponent, Volume),
				OB_PROXY_FIELD(AudioSourceComponent, Pitch),
				OB_PROXY_FIELD(AudioSourceComponent, Loop),
				OB_PROXY_FIELD(AudioSourceComponent, PlayOnStart),
				OB_PROXY_FIELD(AudioSourceComponent, Spatial),
				"Play", [](const AudioSourceProxy& proxy)
				{
					proxy.Get();
					SceneAudio* audio = proxy.Owner.SceneContext->GetAudio();
					return audio && audio->Play(proxy.Owner.Get());
				},
				"Stop", [](const AudioSourceProxy& proxy)
				{
					proxy.Get();
					if (SceneAudio* audio = proxy.Owner.SceneContext->GetAudio())
						audio->StopSource(proxy.Owner.Get());
				},
				"IsPlaying", [](const AudioSourceProxy& proxy)
				{
					proxy.Get();
					SceneAudio* audio = proxy.Owner.SceneContext->GetAudio();
					return audio && audio->IsPlaying(proxy.Owner.Get());
				});
		}

		void RegisterEntity(sol::state& lua, ScriptRuntime& runtime)
		{
			lua.new_usertype<LuaEntity>("Entity", sol::no_constructor,
				"ID", sol::readonly_property([](const LuaEntity& entity) { return static_cast<int64_t>(static_cast<uint64_t>(entity.ID)); }),
				"Name", sol::property(
					[](const LuaEntity& entity) { return entity.Get().GetName(); },
					[](const LuaEntity& entity, const std::string& name) { entity.Get().GetComponent<TagComponent>().Tag = name; }),
				"IsValid", [](const LuaEntity& entity) { return entity.Resolve().IsValid(); },
				"Destroy", [](const LuaEntity& entity)
				{
					if (Entity resolved = entity.Resolve())
						entity.SceneContext->DestroyEntity(resolved);
				},
				"HasComponent", [](const LuaEntity& entity, const std::string& name)
				{
					return FindComponentOrThrow(name).Has(entity.Get());
				},
				"AddComponent", [&runtime](const LuaEntity& entity, const std::string& name, sol::optional<sol::table> data)
				{
					const ComponentInfo& info = FindComponentOrThrow(name);
					Entity resolved = entity.Get();
					if (data)
						info.Deserialize(resolved, ScriptGlue::LuaToJson(*data));
					else
						info.Add(resolved);
					if (name == "Script")
						runtime.InstantiateEntity(resolved);
				},
				"RemoveComponent", [&runtime](const LuaEntity& entity, const std::string& name)
				{
					const ComponentInfo& info = FindComponentOrThrow(name);
					if (!info.Removable)
						throw std::runtime_error("component '" + name + "' cannot be removed");
					Entity resolved = entity.Get();
					if (name == "Script")
						runtime.ReleaseInstance(entity.ID, true);
					if (resolved.IsValid())
						info.Remove(resolved);
				},
				"GetComponentData", [](const LuaEntity& entity, const std::string& name, sol::this_state state)
				{
					const ComponentInfo& info = FindComponentOrThrow(name);
					Entity resolved = entity.Get();
					if (!info.Has(resolved))
						return sol::make_object(state, sol::lua_nil);
					return ScriptGlue::JsonToLua(state, info.Serialize(resolved));
				},
				"SetComponentData", [](const LuaEntity& entity, const std::string& name, const sol::table& data)
				{
					const ComponentInfo& info = FindComponentOrThrow(name);
					info.Deserialize(entity.Get(), ScriptGlue::LuaToJson(data));
				},
				"GetScript", [&runtime](const LuaEntity& entity) { return runtime.GetInstance(entity.ID); },
				"AddScript", [&runtime](const LuaEntity& entity, const std::string& scriptPath, sol::optional<sol::table> fields)
				{
					Entity resolved = entity.Get();
					ScriptComponent component;
					component.Script = scriptPath;
					if (fields)
						component.Fields = ScriptGlue::LuaToJson(*fields);
					if (!component.Fields.is_object())
						component.Fields = json::object();
					runtime.ReleaseInstance(entity.ID, true);
					resolved.AddOrReplaceComponent<ScriptComponent>(std::move(component));
					if (!runtime.InstantiateEntity(resolved))
						throw std::runtime_error("could not instantiate script '" + scriptPath + "'");
					return runtime.GetInstance(entity.ID);
				},
				"Transform", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<TransformComponent>(entity, state); }),
				"Camera", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<CameraComponent>(entity, state); }),
				"MeshRenderer", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<MeshRendererComponent>(entity, state); }),
				"DirectionalLight", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<DirectionalLightComponent>(entity, state); }),
				"PointLight", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<PointLightComponent>(entity, state); }),
				"SkyLight", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<SkyLightComponent>(entity, state); }),
				"RigidBody", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<RigidBodyComponent>(entity, state); }),
				"BoxCollider", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<BoxColliderComponent>(entity, state); }),
				"SphereCollider", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<SphereColliderComponent>(entity, state); }),
				"AudioSource", sol::readonly_property([](const LuaEntity& entity, sol::this_state state) { return GetProxy<AudioSourceComponent>(entity, state); }),
				sol::meta_function::equal_to, [](const LuaEntity& a, const LuaEntity& b) { return a.ID == b.ID && a.SceneContext == b.SceneContext; },
				sol::meta_function::to_string, [](const LuaEntity& entity)
				{
					Entity resolved = entity.Resolve();
					return fmt::format("Entity({}, '{}')", static_cast<uint64_t>(entity.ID), resolved ? resolved.GetName() : std::string("<destroyed>"));
				});
		}

		void RegisterScene(sol::state& lua, ScriptRuntime& runtime)
		{
			Scene* scene = runtime.GetScene();
			auto toObject = [](sol::this_state state, Entity entity)
			{
				if (!entity)
					return sol::make_object(state, sol::lua_nil);
				return sol::make_object(state, LuaEntity{ entity.GetScene(), entity.GetUUID() });
			};

			sol::table sceneTable = lua.create_named_table("Scene");
			sceneTable["GetName"] = [scene]() { return scene->GetName(); };
			sceneTable["CreateEntity"] = [scene](sol::optional<std::string> name)
			{
				Entity entity = scene->CreateEntity(name.value_or("Entity"));
				return LuaEntity{ scene, entity.GetUUID() };
			};
			sceneTable["DestroyEntity"] = [scene](const LuaEntity& entity)
			{
				if (Entity resolved = entity.Resolve())
					scene->DestroyEntity(resolved);
			};
			sceneTable["DuplicateEntity"] = [scene, &runtime](const LuaEntity& entity)
			{
				Entity duplicate = scene->DuplicateEntity(entity.Get());
				if (duplicate.HasComponent<ScriptComponent>())
					runtime.InstantiateEntity(duplicate);
				return LuaEntity{ scene, duplicate.GetUUID() };
			};
			sceneTable["FindEntityByName"] = [scene, toObject](const std::string& name, sol::this_state state)
			{
				return toObject(state, scene->FindEntityByName(name));
			};
			sceneTable["GetEntityByID"] = [scene, toObject](int64_t id, sol::this_state state)
			{
				return toObject(state, scene->GetEntityByUUID(static_cast<uint64_t>(id)));
			};
			sceneTable["GetEntities"] = [scene](sol::this_state state)
			{
				sol::state_view view(state);
				sol::table result = view.create_table();
				int index = 1;
				for (UUID uuid : scene->GetEntityOrder())
					result[index++] = LuaEntity{ scene, uuid };
				return result;
			};
			sceneTable["GetEntitiesWithComponent"] = [scene](const std::string& name, sol::this_state state)
			{
				const ComponentInfo& info = FindComponentOrThrow(name);
				sol::state_view view(state);
				sol::table result = view.create_table();
				int index = 1;
				for (UUID uuid : scene->GetEntityOrder())
				{
					if (info.Has(scene->GetEntityByUUID(uuid)))
						result[index++] = LuaEntity{ scene, uuid };
				}
				return result;
			};
			sceneTable["GetPrimaryCamera"] = [scene, toObject](sol::this_state state)
			{
				return toObject(state, scene->GetPrimaryCameraEntity());
			};
			sceneTable["GetEntityCount"] = [scene]() { return static_cast<int64_t>(scene->GetEntityCount()); };
			sceneTable["Load"] = [scene](const std::string& path) { scene->RequestSceneLoad(path); };
		}

		void RegisterPhysics(sol::state& lua, ScriptRuntime& runtime)
		{
			Scene* scene = runtime.GetScene();
			sol::table physics = lua.create_named_table("Physics");
			physics["GetGravity"] = [scene]()
			{
				PhysicsWorld* world = scene->GetPhysicsWorld();
				return world ? world->GetGravity() : glm::vec3(0.0f, -9.81f, 0.0f);
			};
			physics["SetGravity"] = [scene](const glm::vec3& gravity)
			{
				if (PhysicsWorld* world = scene->GetPhysicsWorld())
					world->SetGravity(gravity);
			};
			physics["Raycast"] = [scene](const glm::vec3& origin, const glm::vec3& direction, sol::optional<float> maxDistance, sol::this_state state)
			{
				PhysicsWorld* world = scene->GetPhysicsWorld();
				std::optional<RaycastHit> hit = world ? world->Raycast(origin, direction, maxDistance.value_or(1000.0f)) : std::nullopt;
				if (!hit)
					return sol::make_object(state, sol::lua_nil);

				sol::state_view view(state);
				sol::table result = view.create_table();
				result["Entity"] = LuaEntity{ scene, hit->Entity };
				result["Position"] = hit->Position;
				result["Normal"] = hit->Normal;
				result["Distance"] = hit->Distance;
				return sol::make_object(state, result);
			};
		}

		void RegisterAudio(sol::state& lua)
		{
			sol::table audio = lua.create_named_table("Audio");
			audio["PlayOneShot"] = [](const std::string& clip, sol::optional<float> volume)
			{
				return AudioEngine::PlayOneShot(Project::ResolveAssetPath(clip), volume.value_or(1.0f));
			};
			audio["SetMasterVolume"] = [](float volume) { AudioEngine::SetMasterVolume(volume); };
			audio["GetMasterVolume"] = []() { return AudioEngine::GetMasterVolume(); };
		}

		void RegisterUI(sol::state& lua, ScriptRuntime& runtime)
		{
			sol::table ui = lua.create_named_table("UI");
			ui["Text"] = [&runtime](const std::string& text, float x, float y, sol::optional<sol::table> options)
			{
				UITextElement element;
				element.Text = text;
				element.Position = glm::vec2(x, y);
				if (options)
				{
					element.Size = options->get_or("Size", element.Size);
					if (sol::optional<glm::vec4> color = options->get<sol::optional<glm::vec4>>("Color"))
						element.Color = *color;
					if (sol::optional<std::string> anchorName = options->get<sol::optional<std::string>>("Anchor"))
					{
						const auto anchor = AnchorFromString(*anchorName);
						if (!anchor)
							throw std::runtime_error("unknown UI anchor '" + *anchorName + "'");
						element.Anchor = *anchor;
					}
				}
				runtime.AddUIText(std::move(element));
			};
		}

	}

	// ------------------------------------------------------------------------------------------------------------

	void ScriptGlue::Register(ScriptRuntime& runtime)
	{
		sol::state& lua = runtime.GetState();
		RegisterMath(lua);
		RegisterCore(lua, runtime);
		RegisterInput(lua);
		RegisterComponents(lua);
		RegisterEntity(lua, runtime);
		RegisterScene(lua, runtime);
		RegisterPhysics(lua, runtime);
		RegisterAudio(lua);
		RegisterUI(lua, runtime);
	}

	sol::object ScriptGlue::JsonToLua(sol::state_view lua, const json& value)
	{
		switch (value.type())
		{
			case json::value_t::null:
			case json::value_t::discarded:
				return sol::make_object(lua, sol::lua_nil);
			case json::value_t::boolean:
				return sol::make_object(lua, value.get<bool>());
			case json::value_t::number_integer:
				return sol::make_object(lua, value.get<int64_t>());
			case json::value_t::number_unsigned:
				return sol::make_object(lua, static_cast<int64_t>(value.get<uint64_t>()));
			case json::value_t::number_float:
				return sol::make_object(lua, value.get<double>());
			case json::value_t::string:
				return sol::make_object(lua, value.get<std::string>());
			case json::value_t::array:
			{
				const bool numeric = std::all_of(value.begin(), value.end(), [](const json& element) { return element.is_number(); });
				if (numeric && value.size() == 2)
					return sol::make_object(lua, glm::vec2(value[0].get<float>(), value[1].get<float>()));
				if (numeric && value.size() == 3)
					return sol::make_object(lua, glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
				if (numeric && value.size() == 4)
					return sol::make_object(lua, glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()));

				sol::table table = lua.create_table(static_cast<int>(value.size()), 0);
				int index = 1;
				for (const json& element : value)
					table[index++] = JsonToLua(lua, element);
				return table;
			}
			case json::value_t::object:
			{
				sol::table table = lua.create_table(0, static_cast<int>(value.size()));
				for (auto it = value.begin(); it != value.end(); ++it)
					table[it.key()] = JsonToLua(lua, it.value());
				return table;
			}
			case json::value_t::binary:
				break;
		}
		return sol::make_object(lua, sol::lua_nil);
	}

	json ScriptGlue::LuaToJson(const sol::object& value, int depth)
	{
		if (depth > MaxConversionDepth)
			throw std::runtime_error("data is nested too deeply (self-referencing table?)");

		switch (value.get_type())
		{
			case sol::type::lua_nil:
			case sol::type::none:
				return nullptr;
			case sol::type::boolean:
				return value.as<bool>();
			case sol::type::number:
			{
				const double number = value.as<double>();
				if (std::floor(number) == number && std::abs(number) < 9007199254740992.0)
					return static_cast<int64_t>(number);
				return number;
			}
			case sol::type::string:
				return value.as<std::string>();
			case sol::type::userdata:
			{
				if (value.is<glm::vec2>())
					return json(value.as<glm::vec2>());
				if (value.is<glm::vec3>())
					return json(value.as<glm::vec3>());
				if (value.is<glm::vec4>())
					return json(value.as<glm::vec4>());
				if (value.is<LuaEntity>())
					return static_cast<uint64_t>(value.as<LuaEntity>().ID);
				throw std::runtime_error("value of this type cannot be converted to data");
			}
			case sol::type::table:
			{
				sol::table table = value.as<sol::table>();
				const size_t length = table.size();
				size_t count = 0;
				for (const auto& entry : table)
				{
					(void)entry;
					count++;
				}

				if (length > 0 && length == count)
				{
					json array = json::array();
					for (size_t i = 1; i <= length; i++)
						array.push_back(LuaToJson(table[i], depth + 1));
					return array;
				}

				json object = json::object();
				for (const auto& [key, element] : table)
				{
					std::string keyString = key.get_type() == sol::type::string ? key.as<std::string>() : std::to_string(static_cast<int64_t>(key.as<double>()));
					object[keyString] = LuaToJson(element, depth + 1);
				}
				return object;
			}
			default:
				throw std::runtime_error("value of this type cannot be converted to data");
		}
	}

}
