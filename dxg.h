#pragma once
#include <span>
#include <vector>
#include <string_view>

#include "magic_enum.h"
#include "common.h"

namespace dxg
{
	struct BoneLink
	{
		int8_t index;
		int8_t parent;
		int8_t child;
		int8_t sibling;
	};

	struct StringList
	{
		uint32_t data_size;

		const char* GetData() const
		{
			return reinterpret_cast<const char*>(this) + sizeof(StringList);
		}

		// guaranteed to be null terminated
		std::vector<std::string_view> Parse() const
		{
			std::vector<std::string_view> result;
			auto name = GetData();
			int items_count = 0;
			do
			{
				auto len = strlen(name);
				if (len <= 0)
				{
					break;
				}
				result.push_back(std::string_view(name, len));
				items_count++;
				name = name + len + 1;
			} while (true);
			return result;
		}
	};

	struct SkeletonHeader
	{
		uint32_t bone_count;
		uint32_t data_size;

		const StringList* GetBoneNames() const
		{
			return reinterpret_cast<const StringList*>(reinterpret_cast<const uint8_t*>(this) + sizeof(SkeletonHeader));
		}

		std::span<const BoneLink> GetBoneLinks() const
		{
			auto names_data = GetBoneNames();
			return {
				reinterpret_cast<const BoneLink*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(SkeletonHeader) +
					names_data->data_size +
					sizeof(StringList)
				),
				bone_count
			};
		}

		std::span<const Matrix4x4> GetBoneMatrices() const
		{
			auto links = GetBoneLinks();
			return {
				reinterpret_cast<const Matrix4x4*>(
					links.data() + links.size()
				),
				bone_count
			};
		}
	};

	struct VertexDataIndices
	{
		int16_t position_index;
		int16_t normal_index;
		int16_t uv_index;
		int16_t uv_2_index;
		int16_t color_index;
	};

	struct Face
	{
		uint16_t indices[3];
	};

	struct WeightIndices
	{
		int8_t indices[3];
	};

	struct MeshHeader
	{
		uint16_t vertex_count;
		uint16_t face_count;
		uint8_t weight_bone_count;
		uint8_t unk5;
		uint8_t unk6;
		uint8_t unk7;
		uint32_t weight_bone_indices_count;
		uint32_t data_size;

		std::span<const VertexDataIndices> GetVertexDataIndices() const
		{
			return {
				reinterpret_cast<const VertexDataIndices*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(MeshHeader)
				),
				vertex_count
			};
		}

		std::span<const Face> GetFaces() const
		{
			auto vertex_indices = GetVertexDataIndices();
			return {
				reinterpret_cast<const Face*>(
					reinterpret_cast<const uint8_t*>(vertex_indices.data() + vertex_indices.size())
				),
				face_count
			};
		}

		const StringList* GetWeightedBoneNames() const
		{
			auto faces = GetFaces();
			return reinterpret_cast<const StringList*>(
				reinterpret_cast<const uint8_t*>(faces.data() + faces.size())
				);
		}

		std::span<const WeightIndices> GetWeightBoneIndices() const
		{
			auto names = GetWeightedBoneNames();
			return {
				reinterpret_cast<const WeightIndices*>(
					reinterpret_cast<const uint8_t*>(names->GetData()) +
					names->data_size
				),
				weight_bone_indices_count / 3
			};
		}
	};

	struct BoneWeights
	{
		float data[2];

		Vector3 GetWeights()
		{
			return { data[0], data[1], 1.f - data[0] - data[1] };
		}
	};

	struct MeshGroupDataHeader
	{
		uint32_t unk0;
		int8_t mesh_count;
		int8_t unk6;
		int16_t unk7;
		uint32_t data_size;
		uint16_t position_count;
		uint16_t normal_count;
		uint16_t uv_1_count;
		uint16_t uv_2_count;
		uint16_t color_count;
		uint32_t weights_count;

		const MeshHeader* GetMeshHeader(int index) const
		{
			auto header = reinterpret_cast<const MeshHeader*>(
				reinterpret_cast<const uint8_t*>(this) +
				sizeof(MeshGroupDataHeader)
				);

			for (int i = 0; i < index; i++)
			{
				header = reinterpret_cast<const MeshHeader*>(
					reinterpret_cast<const uint8_t*>(header) +
					header->data_size +
					sizeof(MeshHeader)
					);
			}

			return header;
		}

		std::span<const Vector3> GetPositions() const
		{
			auto meshes_end = GetMeshHeader(mesh_count);
			return {
				reinterpret_cast<const Vector3*>(meshes_end),
				position_count
			};
		}

		std::span<const Vector3> GetNormals() const
		{
			auto positions = GetPositions();
			return {
				reinterpret_cast<const Vector3*>(positions.data() + positions.size()),
				normal_count
			};
		}

		std::span<const Vector2> GetUVs() const
		{
			auto normals = GetNormals();
			return {
				reinterpret_cast<const Vector2*>(normals.data() + normals.size()),
				uv_1_count
			};
		}

		std::span<const Vector2> GetUVs2() const
		{
			auto uvs = GetUVs();
			return {
				reinterpret_cast<const Vector2*>(uvs.data() + uvs.size()),
				uv_2_count
			};
		}

		std::span<const ColorRGBA> GetColors() const
		{
			auto uv2s = GetUVs2();
			return {
				reinterpret_cast<const ColorRGBA*>(uv2s.data() + uv2s.size()),
				color_count
			};
		}

		std::span<const BoneWeights> GetWeights() const
		{
			auto unk3 = GetColors();
			return {
				reinterpret_cast<const BoneWeights*>(unk3.data() + unk3.size()),
				weights_count / 2
			};
		}
	};

	struct MeshGroupHeader
	{
		uint32_t group_data_count;
		uint32_t data_size;

		const MeshGroupDataHeader* GetMeshGroupDataHeader(int index) const
		{
			auto header = reinterpret_cast<const MeshGroupDataHeader*>(
				reinterpret_cast<const uint8_t*>(this) +
				sizeof(MeshGroupHeader)
				);

			for (int i = 0; i < index; i++)
			{
				header = reinterpret_cast<const MeshGroupDataHeader*>(
					reinterpret_cast<const uint8_t*>(header) +
					header->data_size +
					sizeof(MeshGroupDataHeader)
					);
			}

			return header;
		}
	};

	struct MeshGroupListHeader
	{
		uint32_t group_count;
		uint32_t data_size;

		const StringList* GetGroupNames() const
		{
			return reinterpret_cast<const StringList*>(reinterpret_cast<const uint8_t*>(this) + sizeof(MeshGroupListHeader));
		}

		const MeshGroupHeader* GetMeshGroupHeader(int index) const
		{
			auto names_data = GetGroupNames();

			auto header = reinterpret_cast<const MeshGroupHeader*>(
				reinterpret_cast<const uint8_t*>(this) +
				sizeof(MeshGroupListHeader) +
				names_data->data_size +
				sizeof(StringList)
				);

			for (int i = 0; i < index; i++)
			{
				header = reinterpret_cast<const MeshGroupHeader*>(
					reinterpret_cast<const uint8_t*>(header) +
					header->data_size +
					sizeof(MeshGroupHeader)
					);
			}

			return header;
		}
	};

	struct FileHeader
	{
		enum class EHeaders : uint32_t
		{
			MeshGroupListHeader = 1 << 0,
			SkeletonHeader = 1 << 1,
			UnkHeader1 = 1 << 2,
			UnkHeader2 = 1 << 3,
			UnkHeader3 = 1 << 4,
			UnkHeader4 = 1 << 5
		};

		uint8_t signature[4];
		uint16_t flag_a1;
		uint16_t flag_a2;
		EHeaders present_headers_map;

		uint32_t GetVersion() const
		{
			return flag_a2 | (flag_a1 << 16);
		}

		const MeshGroupListHeader* GetMeshGroupListHeader() const
		{
			using namespace magic_enum::bitwise_operators;

			if ((present_headers_map & EHeaders::MeshGroupListHeader) != EHeaders::MeshGroupListHeader)
			{
				return nullptr;
			}

			return reinterpret_cast<const MeshGroupListHeader*>(reinterpret_cast<const uint8_t*>(this) + sizeof(FileHeader));
		}

		const SkeletonHeader* GetSkeletonHeader() const
		{
			using namespace magic_enum::bitwise_operators;

			if ((present_headers_map & EHeaders::SkeletonHeader) != EHeaders::SkeletonHeader)
			{
				return nullptr;
			}

			auto offset = 0;

			if (auto mesh_header = GetMeshGroupListHeader())
			{
				offset += mesh_header->data_size + sizeof(MeshGroupListHeader);
			}

			return reinterpret_cast<const SkeletonHeader*>(reinterpret_cast<const uint8_t*>(this) + sizeof(FileHeader) + offset);
		}
	};
}