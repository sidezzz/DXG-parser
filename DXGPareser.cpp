// DXGPareser.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <fstream>
#include <span>
#include <format>
#include <cassert>
#include <filesystem>
#include <ranges>

#include <fbxsdk.h>
#include "popl.h"
#include "magic_enum.h"
#include "dxg.h"
#include "mrb.h"

std::vector<uint8_t> ReadFile(std::filesystem::path filename)
{
	// open the file:
	std::ifstream file(filename, std::ios::binary);

	// Stop eating new lines in binary mode!!!
	file.unsetf(std::ios::skipws);

	// get its size:
	std::streampos fileSize;

	file.seekg(0, std::ios::end);
	fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// reserve capacity
	std::vector<uint8_t> vec;
	vec.reserve(fileSize);

	// read the data:
	vec.insert(vec.begin(),
		std::istream_iterator<uint8_t>(file),
		std::istream_iterator<uint8_t>());

	return vec;
}

std::vector<std::string> SplitString(std::string_view str, std::string_view delimiter)
{
	std::vector<std::string> result;

	size_t pos = 0;
	size_t offset = 0;
	while (offset < str.size() && (pos = str.find(delimiter, offset)) != std::string::npos)
	{
		result.emplace_back(str.substr(offset, pos - offset));
		offset = pos + delimiter.size();
	}

	if (offset < str.size())
	{
		result.emplace_back(str.substr(offset, str.size() - offset));
	}

	return result;
}

bool ParseMRBFile(const std::vector<uint8_t> file, fbxsdk::FbxScene* scene)
{
	using namespace magic_enum::bitwise_operators;

	auto mrb_header = reinterpret_cast<const mrb::FileHeader*>(file.data());

	if (strcmp(mrb_header->signature, "MRB") != 0)
	{
		std::cout << std::format("Signature missmatch\n");
		return false;
	}

	if (mrb_header->magic != 9)
	{
		std::cout << std::format("Magic missmatch {}\n", mrb_header->magic);
		return false;
	}

	std::cout << std::format("Mrb entries {}\n", mrb_header->animation_count);

	for (int animation_idx = 0; animation_idx < mrb_header->animation_count; animation_idx++)
	{
		auto animation_header = mrb_header->GetAnimationHeader(animation_idx);
		std::cout << std::format("Located animation '{}', data size {}, bitfield '{}'\n",
			animation_header->name, animation_header->data_size, magic_enum::enum_flags_name(animation_header->data_bitfield));

		constexpr auto required_data_blocks =
			mrb::EAnimationDataType::Bones | mrb::EAnimationDataType::Keyframes |
			mrb::EAnimationDataType::Positions | mrb::EAnimationDataType::Rotations |
			mrb::EAnimationDataType::Scales | mrb::EAnimationDataType::IndexMap;
		if ((animation_header->data_bitfield & required_data_blocks) != required_data_blocks)
		{
			std::cout << std::format("Animation '{}', doesn't have all requiered data blocks {}\n", animation_header->name, magic_enum::enum_flags_name(required_data_blocks));
			continue;
		}

		for (int data_idx = 0; data_idx < 32; data_idx++)
		{
			auto type = static_cast<mrb::EAnimationDataType>(1 << data_idx);
			if (auto block = animation_header->GetDataBlock(type))
			{
				std::cout << std::format("Located data block {} {}, elements count {} element size {}\n", (void*)(block), magic_enum::enum_name(type), block->elements_count, block->element_size);
			}
		}

		auto bones_block = animation_header->GetDataBlock<mrb::BoneNamesBlock>();
		auto keyframes_block = animation_header->GetDataBlock<mrb::KeyframesBlock>();
		auto positions_block = animation_header->GetDataBlock<mrb::PositionsBlock>();
		auto rotations_block = animation_header->GetDataBlock<mrb::RotationsBlock>();
		auto scales_block = animation_header->GetDataBlock<mrb::ScalesBlock>();
		auto index_map_block = animation_header->GetDataBlock<mrb::IndexMapBlock>();

		assert(keyframes_block->element_size == sizeof(uint32_t));
		assert(positions_block->element_size == sizeof(Vector3));
		assert(rotations_block->element_size == sizeof(Vector4));
		assert(scales_block->element_size == sizeof(Vector3));
		assert(index_map_block->element_size == sizeof(mrb::IndexMapElement) * keyframes_block->elements_count);

		if (auto unk4_block = animation_header->GetDataBlock<mrb::Unk4Block>())
		{
			std::cout << std::format("Unk4 {}\n", unk4_block->GetData()[0]);
		}
		/*for (auto& name : bones_block->GetBoneNames())
		{
			std::cout << std::format("Located bone '{}'\n", name);
		}
		for (auto keyframe : keyframes_block->GetKeyframes())
		{
			std::cout << std::format("Located keyframe {}\n", keyframe);
		}
		for (auto position : positions_block->GetPositions())
		{
			std::cout << std::format("Located position x {} y {} z {}\n", position.x, position.y, position.z);
		}
		for (auto indices : index_map_block->GetIndexes(keyframes_block->elements_count))
		{
			std::cout << std::format("Located indices p {} r {} s {}\n", indices.position_index, indices.rotation_index, indices.scale_index);
		}*/

		auto bone_names = bones_block->GetBoneNames();
		auto keyframes = keyframes_block->GetKeyframes();
		auto positions = positions_block->GetPositions();
		auto rotations = rotations_block->GetRotations();
		auto scales = scales_block->GetScales();
		auto indices_map = index_map_block->GetIndexes();

		assert(index_map_block->elements_count == bone_names.size());

		auto anim_stack = fbxsdk::FbxAnimStack::Create(scene, animation_header->name);
		auto anim_layer = fbxsdk::FbxAnimLayer::Create(anim_stack->GetFbxManager(), std::format("{}_Layer", animation_header->name).c_str());
		anim_stack->AddMember(anim_layer);

		for (int bone_idx = 0; bone_idx < bone_names.size(); bone_idx++)
		{
			auto bone_name = bone_names[bone_idx];
			auto bone = scene->GetRootNode()->FindChild(std::string(bone_name).c_str());
			if (bone == nullptr)
			{
				std::cout << std::format("Bone '{}' not found in skeleton\n", bone_name);
				continue;
			}

			std::cout << std::format("Animating bone '{}'\n", bone_name);

			fbxsdk::FbxAnimCurve* translation_curves[3];
			fbxsdk::FbxAnimCurve* rotation_curves[3];
			fbxsdk::FbxAnimCurve* scale_curves[3];

			translation_curves[0] = bone->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
			translation_curves[1] = bone->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			translation_curves[2] = bone->LclTranslation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			rotation_curves[0] = bone->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
			rotation_curves[1] = bone->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			rotation_curves[2] = bone->LclRotation.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			scale_curves[0] = bone->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_X, true);
			scale_curves[1] = bone->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			scale_curves[2] = bone->LclScaling.GetCurve(anim_layer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			for (int i = 0; i < 3; i++)
			{
				translation_curves[i]->KeyModifyBegin();
				rotation_curves[i]->KeyModifyBegin();
				scale_curves[i]->KeyModifyBegin();
			}

			for (int keyframe_idx = 0; keyframe_idx < keyframes.size(); keyframe_idx++)
			{
				auto keyframe = keyframes[keyframe_idx];
				auto indices = indices_map[bone_idx * keyframes.size() + keyframe_idx];

				auto position = positions[indices.position_index];
				auto rotation_quat = rotations[indices.rotation_index];
				auto scale = scales[indices.scale_index];

				fbxsdk::FbxAMatrix rotation_matrix;
				rotation_matrix.SetQ(fbxsdk::FbxQuaternion(rotation_quat.x, rotation_quat.y, rotation_quat.z, rotation_quat.w));
				auto rotation = rotation_matrix.GetR();

				fbxsdk::FbxTime time;
				time.SetMilliSeconds(keyframe);

				for (int i = 0; i < 3; i++)
				{
					translation_curves[i]->KeySet(translation_curves[i]->KeyAdd(time), time, position.raw[i], fbxsdk::FbxAnimCurveDef::eInterpolationLinear);
					rotation_curves[i]->KeySet(rotation_curves[i]->KeyAdd(time), time, rotation.Buffer()[i], fbxsdk::FbxAnimCurveDef::eInterpolationLinear);
					scale_curves[i]->KeySet(scale_curves[i]->KeyAdd(time), time, scale.raw[i], fbxsdk::FbxAnimCurveDef::eInterpolationLinear);
				}

				/*std::cout << std::format("Keyframe {}\n", keyframe);
				std::cout << std::format("Position x {} y {} z {}\n", position.x, position.y, position.z);
				std::cout << std::format("Rotation x {} y {} z {} w {}\n", rotation.x, rotation.y, rotation.z, rotation.w);
				std::cout << std::format("Scale x {} y {} z {}\n", scale.x, scale.y, scale.z);*/
			}

			fbxsdk::FbxAnimCurveFilterUnroll unroll_filter;
			unroll_filter.SetForceAutoTangents(true);
			unroll_filter.Apply(rotation_curves, 3);

			std::cout << std::format("Added {} keyframses\n", keyframes.size());

			for (int i = 0; i < 3; i++)
			{
				translation_curves[i]->KeyModifyEnd();
				rotation_curves[i]->KeyModifyEnd();
				scale_curves[i]->KeyModifyEnd();
			}
		}
	}

	return true;
}

class DxgParser
{
public:
	DxgParser(std::string_view path)
	{
		_dxg_file_data = ReadFile(path);
		if (_dxg_file_data.empty())
		{
			throw std::invalid_argument(std::format("Failed to read dxg file '{}'\n", path));
		}
	}

	void BeginParse()
	{
		auto file_header = reinterpret_cast<const dxg::FileHeader*>(_dxg_file_data.data());

		std::cout << std::format("Present headers '{}'\n", magic_enum::enum_flags_name(file_header->present_headers_map));
		std::cout << std::format("DXG Version 0x{:X}\n", file_header->GetVersion());

		_fbx_manager = fbxsdk::FbxManager::Create();
		_scene = fbxsdk::FbxScene::Create(_fbx_manager, "DXG");
		// not sure about this
		fbxsdk::FbxAxisSystem initial_axis_system;
		fbxsdk::FbxAxisSystem::ParseAxisSystem("XyZ", initial_axis_system);
		_scene->GetGlobalSettings().SetAxisSystem(initial_axis_system);

		auto root_node = fbxsdk::FbxNode::Create(_fbx_manager, "Root");
		_scene->GetRootNode()->AddChild(root_node);

		if (auto skeleton_header = file_header->GetSkeletonHeader())
		{
			std::cout << std::format("Located skeleton header, data size {}\n", skeleton_header->data_size);

			auto skeleton_bone_names = skeleton_header->GetBoneNames()->Parse();
			auto links = skeleton_header->GetBoneLinks();
			auto matrices = skeleton_header->GetBoneMatrices();

			assert(skeleton_bone_names.size() == links.size());
			assert(skeleton_bone_names.size() == matrices.size());

			std::vector<fbxsdk::FbxNode*> skeleton_bone_nodes;
			for (auto&& name : skeleton_bone_names)
			{
				skeleton_bone_nodes.push_back(fbxsdk::FbxNode::Create(_fbx_manager, name.data()));
			}

			for (int i = 0; i < skeleton_bone_names.size(); i++)
			{
				const auto& name = skeleton_bone_names[i];
				auto link = links[i];
				auto matrix = matrices[i].ToFbxMatrix();
				auto bone_node = skeleton_bone_nodes[i];

				auto local_to_parent = matrix.Inverse();

				auto skeleton_attribute = fbxsdk::FbxSkeleton::Create(_fbx_manager, "");
				if (link.parent == -1)
				{
					skeleton_attribute->SetSkeletonType(fbxsdk::FbxSkeleton::EType::eRoot);
					root_node->AddChild(bone_node);
				}
				else
				{
					skeleton_attribute->SetSkeletonType(fbxsdk::FbxSkeleton::EType::eLimbNode);
					skeleton_bone_nodes[link.parent]->AddChild(bone_node);
					local_to_parent = matrices[link.parent].ToFbxMatrix() * local_to_parent;
				}

				auto translation = local_to_parent.GetT();
				auto rotation = local_to_parent.GetR();
				auto scale = local_to_parent.GetS();

				std::cout << std::format("Located bone '{}'\n", name);
				/*std::cout << std::format("Translation x {} y {} z {}\n", translation.Buffer()[0], translation.Buffer()[1], translation.Buffer()[2]);
				std::cout << std::format("Rotation x {} y {} z {}\n", rotation.Buffer()[0], rotation.Buffer()[1], rotation.Buffer()[2]);
				std::cout << std::format("Scale x {} y {} z {}\n", scale.Buffer()[0], scale.Buffer()[1], scale.Buffer()[2]);*/

				bone_node->LclTranslation.Set(translation);
				bone_node->LclRotation.Set(rotation);
				bone_node->LclScaling.Set(scale);

				bone_node->SetNodeAttribute(skeleton_attribute);
			}
		}
	}

	void AttachMrb(std::string_view anim_file, bool inline_)
	{
		std::cout << std::format("Reading MRB file '{}'\n", anim_file);
		auto path = std::filesystem::path(anim_file);
		auto file = ReadFile(path);

		if (file.empty())
		{
			throw std::invalid_argument(std::format("Failed to read MRB file '{}'\n", anim_file));
		}


		auto animation_scene = _scene;
		if (!inline_)
		{
			animation_scene = static_cast<fbxsdk::FbxScene*>(_scene->Clone(fbxsdk::FbxObject::eDeepClone));
			animation_scene->SetName(path.filename().replace_extension().string().c_str());
			_animations_scenes.push_back(animation_scene);
		}

		if (!ParseMRBFile(file, animation_scene))
		{
			throw std::invalid_argument(std::format("Failed to parse MRB '{}'\n", anim_file));
		}
	}

	void EndParse(std::string_view output_folder)
	{
		auto file_header = reinterpret_cast<const dxg::FileHeader*>(_dxg_file_data.data());
		auto root_node = _scene->GetRootNode()->FindChild("Root");

		if (auto mesh_group_list_header = file_header->GetMeshGroupListHeader())
		{
			std::cout << std::format("Located mesh group list header, data size {}\n", mesh_group_list_header->data_size);

			auto group_names = mesh_group_list_header->GetGroupNames()->Parse();

			assert(mesh_group_list_header->group_count == group_names.size());

			for (int mesh_group_idx = 0; mesh_group_idx < mesh_group_list_header->group_count; mesh_group_idx++)
			{
				auto& group_name = group_names[mesh_group_idx];
				auto mesh_group_header = mesh_group_list_header->GetMeshGroupHeader(mesh_group_idx);

				std::cout << std::format("Located mesh group header '{}', data size {}\n", group_name, mesh_group_header->data_size);

				auto group_node = fbxsdk::FbxNode::Create(_fbx_manager, group_name.data());
				root_node->AddChild(group_node);

				

				if (file_header->GetVersion() < 0x10002)
				{
					// Parse method for 10001 or lower
					std::cout << std::format("Unimplemented version\n");
				}
				else if (false)//file_header->GetVersion() == 0x10002 || file_header->GetVersion() == 0x10003)
				{
					// Parse method for 10002 and 10003
					std::cout << std::format("Unimplemented version\n");
				}
				else
				{
					auto mesh_node = group_node;
					auto mesh_attribute = fbxsdk::FbxMesh::Create(_fbx_manager, "");
					auto skin_deformer = fbxsdk::FbxSkin::Create(_fbx_manager, "");
					mesh_attribute->AddDeformer(skin_deformer);
					mesh_node->SetNodeAttribute(mesh_attribute);

					auto geometry_element_normal = mesh_attribute->CreateElementNormal();
					geometry_element_normal->SetMappingMode(FbxGeometryElement::eByControlPoint);
					geometry_element_normal->SetReferenceMode(FbxGeometryElement::eDirect);
					auto geometry_element_uv_1 = mesh_attribute->CreateElementUV("uv1");
					geometry_element_uv_1->SetMappingMode(FbxGeometryElement::eByControlPoint);
					geometry_element_uv_1->SetReferenceMode(FbxGeometryElement::eDirect);
					auto geometry_element_uv_2 = mesh_attribute->CreateElementUV("uv2");
					geometry_element_uv_2->SetMappingMode(FbxGeometryElement::eByControlPoint);
					geometry_element_uv_2->SetReferenceMode(FbxGeometryElement::eDirect);

					// weird hack to get rid of double vertex color layer
					auto redudant_element = mesh_attribute->CreateElementVertexColor();
					if (mesh_attribute->GetElementVertexColorCount() > 1)
					{
						mesh_attribute->RemoveElementVertexColor(redudant_element);
					}

					auto geometry_element_color = mesh_attribute->GetElementVertexColor();
					geometry_element_color->SetMappingMode(FbxGeometryElement::eByControlPoint);
					geometry_element_color->SetReferenceMode(FbxGeometryElement::eDirect);

					size_t control_point_count = 0;
					for (int group_data_idx = 0; group_data_idx < mesh_group_header->group_data_count; group_data_idx++)
					{
						auto mesh_group_data_header = mesh_group_header->GetMeshGroupDataHeader(group_data_idx);
						for (int mesh_idx = 0; mesh_idx < mesh_group_data_header->mesh_count; mesh_idx++)
						{
							auto mesh_header = mesh_group_data_header->GetMeshHeader(mesh_idx);
							control_point_count += mesh_header->GetVertexDataIndices().size();
						}
					}

					mesh_attribute->InitControlPoints(control_point_count);
					size_t control_points_offset = 0;

					for (int group_data_idx = 0; group_data_idx < mesh_group_header->group_data_count; group_data_idx++)
					{
						auto mesh_group_data_header = mesh_group_header->GetMeshGroupDataHeader(group_data_idx);
						std::cout << std::format(
							"Located mesh group data header {}, data size {}, positions {}, normals {}, "
							"uv_1_count {}, uv_2_count {}, colors {}, weights {}\n",
							group_data_idx, mesh_group_data_header->data_size, mesh_group_data_header->position_count,
							mesh_group_data_header->normal_count, mesh_group_data_header->uv_1_count,
							mesh_group_data_header->uv_2_count, mesh_group_data_header->color_count,
							mesh_group_data_header->weights_count
						);

						assert(!(mesh_group_data_header->weights_count % mesh_group_data_header->position_count));

						/*for (auto pos : mesh_group_data_header->GetPositions())
						{
							std::cout << std::format(
								"Position x {} y {} z {}\n", pos.x, pos.y, pos.z
							);
						}*/

						/*for (auto normal : mesh_group_data_header->GetNormals())
						{
							std::cout << std::format(
								"Normal x {} y {} z {}\n", normal.x, normal.y, normal.z
							);
						}*/

						/*for (auto uv : mesh_group_data_header->GetUVs())
						{
							std::cout << std::format(
								"UV x {} y {}\n", uv.x, uv.y
							);
						}*/

						/*for (auto uv2 : mesh_group_data_header->GetUVs2())
						{
							std::cout << std::format(
								"UV2 {} {}\n", uv2.x, uv2.y
							);
						}*/

						/*for (auto color : mesh_group_data_header->GetColors())
						{
							std::cout << std::format(
								"Color {:02X} {:02X} {:02X} {:02X}\n", color.R, color.G, color.B, color.A
							);
						}*/

						/*for (auto weight_data : mesh_group_data_header->GetWeights())
						{
							auto weight = weight_data.GetWeights();
							std::cout << std::format(
								"Weight {} {} {}\n", weight.x, weight.y, weight.z
							);
						}*/

						for (int mesh_idx = 0; mesh_idx < mesh_group_data_header->mesh_count; mesh_idx++)
						{
							auto mesh_header = mesh_group_data_header->GetMeshHeader(mesh_idx);

							std::cout << std::format("Located mesh header {}, data size {}, weighted bones {}, vertices {}, faces {}, weight bone indices {} unk5 {} unk6 {} unk7 {}\n",
								mesh_idx, mesh_header->data_size, mesh_header->weight_bone_count, mesh_header->vertex_count,
								mesh_header->face_count, mesh_header->weight_bone_indices_count, mesh_header->unk5,
								mesh_header->unk6, mesh_header->unk7
							);

							assert(mesh_header->weight_bone_count <= 8);
							assert(mesh_header->weight_bone_indices_count == 0 || mesh_header->vertex_count * 3 == mesh_header->weight_bone_indices_count);

							auto vertices_data = mesh_header->GetVertexDataIndices();
							auto faces = mesh_header->GetFaces();
							auto weight_bone_indices = mesh_header->GetWeightBoneIndices();

							std::vector<std::string_view> weighted_bone_names;
							if (mesh_header->weight_bone_count)
							{
								weighted_bone_names = mesh_header->GetWeightedBoneNames()->Parse();
								assert(mesh_header->weight_bone_count == weighted_bone_names.size());

								for (auto& weight_bone_name : weighted_bone_names)
								{
									auto bone_node = root_node->FindChild(std::string(weight_bone_name).c_str());

									if (bone_node)
									{
										bool cluster_exists = false;
										for (int cluster_i = 0; cluster_i < skin_deformer->GetClusterCount(); cluster_i++)
										{
											auto cluster = skin_deformer->GetCluster(cluster_i);
											if (weight_bone_name == cluster->GetName())
											{
												cluster_exists = true;
												break;
											}
										}

										if (!cluster_exists)
										{
											auto cluster = fbxsdk::FbxCluster::Create(_fbx_manager, bone_node->GetName());
											cluster->SetLink(bone_node);
											cluster->SetLinkMode(fbxsdk::FbxCluster::eTotalOne);
											cluster->SetTransformLinkMatrix(bone_node->EvaluateGlobalTransform());
											skin_deformer->AddCluster(cluster);
										}
									}
									else
									{
										throw std::logic_error(std::format("Mesh is influenced by unknown bone '{}'\n", weight_bone_name));
									}
								}
							}
							else
							{
								std::cout << std::format("Mesh is not skinned\n");
							}

							for (int i = 0; i < vertices_data.size(); i++)
							{
								auto vertex_indices = vertices_data[i];

								auto position = mesh_group_data_header->GetPositions()[vertex_indices.position_index];
								auto normal = mesh_group_data_header->GetNormals()[vertex_indices.normal_index];
								auto uv = mesh_group_data_header->GetUVs()[vertex_indices.uv_index];

								mesh_attribute->GetControlPoints()[control_points_offset + i] = fbxsdk::FbxVector4(position.x, position.y, position.z);
								geometry_element_normal->GetDirectArray().Add(fbxsdk::FbxVector4(normal.x, normal.y, normal.z));
								geometry_element_uv_1->GetDirectArray().Add(fbxsdk::FbxVector2(uv.x, 1.0 - uv.y));

								if (mesh_group_data_header->uv_2_count)
								{
									auto uv2 = mesh_group_data_header->GetUVs2()[vertex_indices.uv_2_index];
									geometry_element_uv_2->GetDirectArray().Add(fbxsdk::FbxVector2(uv2.x, 1.0 - uv2.y));
								}

								if (mesh_group_data_header->color_count)
								{
									auto color = mesh_group_data_header->GetColors()[vertex_indices.color_index];
									geometry_element_color->GetDirectArray().Add(color.ToFbxColor());
								}

								if (mesh_header->weight_bone_count && mesh_header->weight_bone_indices_count)
								{
									auto bone_indices = weight_bone_indices[i];
									auto bone_weights = mesh_group_data_header->GetWeights()[vertex_indices.position_index];

									for (int j = 2; j >= 0; j--)
									{
										fbxsdk::FbxCluster* cluster = nullptr;
										for (int cluster_i = 0; cluster_i < skin_deformer->GetClusterCount(); cluster_i++)
										{
											auto c = skin_deformer->GetCluster(cluster_i);
											if (weighted_bone_names[bone_indices.indices[j]] == c->GetName())
											{
												cluster = c;
												break;
											}
										}

										cluster->AddControlPointIndex(control_points_offset + i, bone_weights.GetWeights().raw[j]);
									}
								}
							}

							for (int i = 0; i < faces.size(); i++)
							{
								auto face = faces[i];

								mesh_attribute->BeginPolygon(-1, -1, -1, false);
								mesh_attribute->AddPolygon(control_points_offset + face.indices[0]);
								mesh_attribute->AddPolygon(control_points_offset + face.indices[1]);
								mesh_attribute->AddPolygon(control_points_offset + face.indices[2]);
								mesh_attribute->EndPolygon();
							}

							control_points_offset += vertices_data.size();

							/*for (int indices_i = 0; indices_i < weight_bone_indices.size(); indices_i++)
							{
								auto weights_index = vertices_data[indices_i].position_index;
								auto bone_indices = weight_bone_indices[indices_i];
								auto bone_weights = mesh_group_data_header->GetWeights()[weights_index];
								std::cout << std::format("Weight bones {} {} {} '{}' '{}' '{}' weights {} {} {}\n",
									bone_indices.indices[0],
									bone_indices.indices[1],
									bone_indices.indices[2],
									skin_deformer->GetCluster(bone_indices.indices[0])->GetName(),
									skin_deformer->GetCluster(bone_indices.indices[1])->GetName(),
									skin_deformer->GetCluster(bone_indices.indices[2])->GetName(),
									bone_weights.GetWeights().raw[0],
									bone_weights.GetWeights().raw[1],
									bone_weights.GetWeights().raw[2]
								);
							}*/
							/*for (auto& bone_name : ParseNames(mesh_header->GetWeightedBoneNamesData()))
							{
								std::cout << std::format("Weight bone {}\n",
									bone_name
								);
							}*/

							/*for (auto weight_indices : mesh_header->GetWeightBoneIndices())
							{
								std::cout << std::format("Weight bone indices {} {} {}\n",
									weight_indices.indices[0], weight_indices.indices[1], weight_indices.indices[2]
								);
							}*/

							/*for (auto vetex_indices : mesh_header->GetVertexDataIndices())
							{
								std::cout << std::format("Vertex indices: pos {}, normal {}, uv {} uv2 {} color {}\n",
									vetex_indices.position_index, vetex_indices.normal_index, vetex_indices.uv_index,
									vetex_indices.uv_2_index, vetex_indices.color_index
								);
							}*/

							/*for (auto face : mesh_header->GetFaces())
							{
								std::cout << std::format("Face {} {} {}\n",
									face.indices[0], face.indices[1], face.indices[2]
								);
							}*/
						}
					}
				}
			}
		}

		fbxsdk::FbxAxisSystem axis_system;
		fbxsdk::FbxAxisSystem::ParseAxisSystem("Xyz", axis_system);

		auto ios = fbxsdk::FbxIOSettings::Create(_fbx_manager, IOSROOT);
		_fbx_manager->SetIOSettings(ios);
		std::filesystem::create_directory(output_folder);

		axis_system.DeepConvertScene(_scene);
		Export(std::format("{}\\output.fbx", output_folder), _scene);
		for (auto&& anim_scene : _animations_scenes)
		{
			axis_system.DeepConvertScene(anim_scene);
			Export(std::format("{}\\output.{}.fbx", output_folder, anim_scene->GetName()), anim_scene);
		}

		// idk what is actual coordinate system so bruteforce all possible
		// coordinate system and import them to blender to select and use one that looks good lol
		//char axis_symbols[3][2] = { {'x', 'X'}, {'y', 'Y'},{'z', 'Z'} };
		//fbxsdk::FbxAxisSystem axis_system;
		//const char* variants[6][3] = {
		//	{ axis_symbols[0], axis_symbols[1], axis_symbols[2] },
		//	{ axis_symbols[0], axis_symbols[2], axis_symbols[1] },
		//	{ axis_symbols[1], axis_symbols[0], axis_symbols[2] },
		//	{ axis_symbols[1], axis_symbols[2], axis_symbols[0] },
		//	{ axis_symbols[2], axis_symbols[1], axis_symbols[0] },
		//	{ axis_symbols[2], axis_symbols[0], axis_symbols[1] }
		//};
		//char axis[4] = {};
		//for (int i = 0; i < 6; i++)
		//{
		//	auto variant = variants[i];
		//	for (int j = 0; j < 8; j++)
		//	{
		//		axis[0] = variant[0][(j & 1) != 0];
		//		axis[1] = variant[1][(j & 2) != 0];
		//		axis[2] = variant[2][(j & 4) != 0];
		//		fbxsdk::FbxAxisSystem::ParseAxisSystem(axis, axis_system);

		//		//root_node->LclTranslation.Set(FbxDouble3(0, 0, 0));

		//		axis_system.DeepConvertScene(_scene);

		//		//root_node->LclTranslation.Set(FbxDouble3(i* j * 500, 0, 0));
		//		root_node->SetName(axis);

		//		Export(std::format("{}\\output_{}_{}_{}.fbx", output_folder, axis, i, j), _scene);
		//	}
		//}

		_dxg_file_data.clear();
		_fbx_manager->Destroy();
		_fbx_manager = nullptr;
		_scene = nullptr;
		_animations_scenes.clear();
	}

private:
	void Export(const std::string& path, fbxsdk::FbxScene* scene)
	{
		auto exporter = fbxsdk::FbxExporter::Create(_fbx_manager, "");
		std::cout << std::format("Exporting '{}'\n", path);
		exporter->Initialize(path.c_str(), -1, _fbx_manager->GetIOSettings());

		/*auto rotation = scene->GetRootNode()->LclRotation.Get();
		rotation.Buffer()[0] += 180.0;
		scene->GetRootNode()->LclRotation.Set(rotation);*/
		if (!exporter->Export(scene))
		{
			throw std::logic_error(std::format("Failed to export scene\n"));
		}
		exporter->Destroy();
	}


	std::vector<uint8_t> _dxg_file_data;
	fbxsdk::FbxManager* _fbx_manager;
	fbxsdk::FbxScene* _scene;
	std::vector<fbxsdk::FbxScene*> _animations_scenes;
};

int main(int argc, char** argv)
{
	try
	{
		popl::OptionParser op("Options");

		auto help_option = op.add<popl::Switch>("h", "help", "produce help message");
		auto input_option = op.add<popl::Value<std::string>, popl::Attribute::required>("i", "input", ".dxg model file input path");
		auto output_option = op.add<popl::Value<std::string>, popl::Attribute::required>("o", "output", "output folder path");
		auto mrb_option = op.add<popl::Value<std::string>>("m", "mrb", ".mrb file list separated with ';'");
		auto inline_option = op.add<popl::Switch>("l", "inline", "inline animations into fbx model");
		op.parse(argc, argv);

		if (std::ranges::views::filter(op.options(), [](auto&& opt)
			{
				return opt->is_set();
			}).empty() || help_option->is_set())
		{
			std::cout << op << '\n';
			return 0;
		}

		DxgParser parser(input_option->value());

		parser.BeginParse();

		if (mrb_option->is_set())
		{
			for (auto&& anim_file : SplitString(mrb_option->value(), ";"))
			{
				parser.AttachMrb(anim_file, inline_option->is_set());
			}
		}

		parser.EndParse(output_option->value());
	}
	catch (std::exception e)
	{
		std::cout << e.what() << '\n';
		return -228;
	}
}