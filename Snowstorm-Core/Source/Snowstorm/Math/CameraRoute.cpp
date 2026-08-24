#include "CameraRoute.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		bool ReadVec3(const nlohmann::json& node, glm::vec3& v)
		{
			if (!node.is_array() || node.size() != 3)
			{
				return false;
			}
			v = {node[0].get<float>(), node[1].get<float>(), node[2].get<float>()};
			return true;
		}
	}

	bool LoadCameraRoute(const std::string& path, CameraRoute& out)
	{
		out = {};

		std::ifstream in(path);
		if (!in.is_open())
		{
			SS_CORE_ERROR("Camera route '{}' could not be opened.", path);
			return false;
		}

		nlohmann::json j;
		try
		{
			in >> j;
		}
		catch (const nlohmann::json::parse_error& e)
		{
			SS_CORE_ERROR("Camera route '{}' is not valid JSON: {}", path, e.what());
			return false;
		}

		if (!j.contains("Waypoints") || !j["Waypoints"].is_array())
		{
			SS_CORE_ERROR("Camera route '{}' has no Waypoints array.", path);
			return false;
		}

		std::vector<CameraWaypoint> waypoints;
		for (const auto& node : j["Waypoints"])
		{
			CameraWaypoint wp;
			if (!node.contains("Position") || !ReadVec3(node["Position"], wp.Position) ||
			    !node.contains("LookAt") || !ReadVec3(node["LookAt"], wp.LookAt))
			{
				SS_CORE_ERROR("Camera route '{}' has a waypoint missing a 3-element Position/LookAt.", path);
				return false;
			}
			waypoints.push_back(wp);
		}

		if (waypoints.size() < 2)
		{
			SS_CORE_ERROR("Camera route '{}' needs at least 2 waypoints, has {}.", path, waypoints.size());
			return false;
		}

		out.Waypoints = std::move(waypoints);
		out.Loop = j.value("Loop", false);
		out.Speed = j.value("Speed", 2.0f);
		out.ArcTable = BuildSplineArcTable(out.Waypoints, out.Loop);

		if (out.Length() <= 0.0f)
		{
			SS_CORE_ERROR("Camera route '{}' has zero arc length (coincident waypoints?).", path);
			out = {};
			return false;
		}

		SS_CORE_INFO("Camera route '{}': {} waypoints, {:.2f} world units at {:.2f} u/s ({:.1f}s), loop={}.",
		             path, out.Waypoints.size(), out.Length(), out.Speed, out.Length() / out.Speed, out.Loop);
		return true;
	}
}
