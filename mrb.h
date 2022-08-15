#pragma once
#include <span>
#include <vector>
#include <string_view>

#include "magic_enum.h"
#include "common.h"

namespace mrb
{
	struct IndexMapElement
	{
		uint16_t position_index;
		uint16_t rotation_index;
		uint16_t scale_index;
	};

	enum class EAnimationDataType : uint32_t
	{
		Bones = 1 << 0,
		Keyframes = 1 << 1,
		Unk2 = 1 << 2,
		Unk3 = 1 << 3,
		Unk4 = 1 << 4,
		Positions = 1 << 5,
		Rotations = 1 << 6,
		Scales = 1 << 7,
		IndexMap = 1 << 8
	};

	struct AnimationDataBlock
	{
		uint32_t elements_count;
		uint32_t element_size;
	};

	struct BoneNamesBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Bones;

		std::vector<std::string_view> GetBoneNames() const
		{
			std::vector<std::string_view> result;

			auto name = reinterpret_cast<const char*>(
				reinterpret_cast<const uint8_t*>(this) +
				sizeof(AnimationDataBlock)
				);
			do
			{
				auto len = strlen(name);
				if (len <= 0)
				{
					break;
				}
				result.push_back(std::string_view(name, len));
				name = name + len + 1;
			} while (true);
			return result;
		}
	};

	struct KeyframesBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Keyframes;

		std::span<const uint32_t> GetKeyframes() const
		{
			return {
				reinterpret_cast<const uint32_t*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count
			};
		}
	};

	struct PositionsBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Positions;

		std::span<const Vector3> GetPositions() const
		{
			return {
				reinterpret_cast<const Vector3*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count
			};
		}
	};

	struct RotationsBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Rotations;

		std::span<const Vector4> GetRotations() const
		{
			return {
				reinterpret_cast<const Vector4*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count
			};
		}
	};

	struct ScalesBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Scales;

		std::span<const Vector3> GetScales() const
		{
			return {
				reinterpret_cast<const Vector3*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count
			};
		}
	};

	struct IndexMapBlock : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::IndexMap;

		std::span<const IndexMapElement> GetIndexes() const
		{
			return {
				reinterpret_cast<const IndexMapElement*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count * element_size / sizeof(IndexMapElement)
			};
		}
	};

	struct Unk4Block : AnimationDataBlock
	{
		constexpr static auto TYPE = EAnimationDataType::Unk4;

		std::span<const uint32_t> GetData() const
		{
			return {
				reinterpret_cast<const uint32_t*>(
					reinterpret_cast<const uint8_t*>(this) +
					sizeof(AnimationDataBlock)
				),
				elements_count 
			};
		}
	};

	struct AnimationHeader
	{
		uint32_t unk0;
		char name[28];
		uint32_t unk20;
		uint32_t data_size; // already contains sizeof(AnimationHeader)
		EAnimationDataType data_bitfield;

		const AnimationDataBlock* GetDataBlock(EAnimationDataType type) const
		{
			using namespace magic_enum::bitwise_operators;

			int offset = 0;

			for (int data_idx = 0; data_idx < 32; data_idx++)
			{
				auto current_type = static_cast<EAnimationDataType>(1 << data_idx);
				if ((data_bitfield & current_type) == current_type)
				{
					auto block = reinterpret_cast<const AnimationDataBlock*>(
						reinterpret_cast<const uint8_t*>(this) +
						sizeof(AnimationHeader) +
						offset
						);

					if (current_type == type)
					{
						return block;
					}

					offset += block->elements_count * block->element_size + sizeof(AnimationDataBlock);

					// align to 4 bytes
					constexpr auto alignment = 4;
					constexpr auto mask = alignment - 1;
					offset = offset + (-offset & mask);
				}
			}

			return nullptr;
		}

		template<class T>
		const T* GetDataBlock() const
		{
			return static_cast<const T*>(GetDataBlock(T::TYPE));
		}
	};

	struct FileHeader
	{
		char signature[4];
		uint32_t magic;
		uint32_t animation_count;

		const AnimationHeader* GetAnimationHeader(int index) const
		{
			auto header = reinterpret_cast<const AnimationHeader*>(
				reinterpret_cast<const uint8_t*>(this) +
				sizeof(FileHeader)
				);

			for (int i = 0; i < index; i++)
			{
				header = reinterpret_cast<const AnimationHeader*>(
					reinterpret_cast<const uint8_t*>(header) +
					header->data_size
					);
			}
			return header;
		}
	};
}