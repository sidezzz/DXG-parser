#pragma once

#include "fbxsdk.h"

struct Matrix4x4
{
	union
	{
		float m[4][4];
		float raw[16]{};
	};

	fbxsdk::FbxAMatrix ToFbxMatrix() const
	{
		fbxsdk::FbxAMatrix fbx_matrix;
		fbx_matrix.SetRow(0, FbxVector4(raw[0], raw[1], raw[2], raw[3]));
		fbx_matrix.SetRow(1, FbxVector4(raw[4], raw[5], raw[6], raw[7]));
		fbx_matrix.SetRow(2, FbxVector4(raw[8], raw[9], raw[10], raw[11]));
		fbx_matrix.SetRow(3, FbxVector4(raw[12], raw[13], raw[14], raw[15]));
		return fbx_matrix;
	}
};

struct Vector4
{
	union
	{
		struct
		{
			float x;
			float y;
			float z;
			float w;
		};
		float raw[4];
	};
};

struct Vector3
{
	union
	{
		struct
		{
			float x;
			float y;
			float z;
		};
		float raw[3];
	};
};

struct Vector2
{
	float x;
	float y;
};

struct ColorRGBA
{
	uint8_t R;
	uint8_t G;
	uint8_t B;
	uint8_t A;

	fbxsdk::FbxColor ToFbxColor() const
	{	
		return fbxsdk::FbxColor(
			static_cast<double>(R) / 255.0,
			static_cast<double>(G) / 255.0,
			static_cast<double>(B) / 255.0,
			static_cast<double>(A) / 255.0
		);
	}
};