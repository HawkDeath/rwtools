#include "renderware.h"

#include <cstdio>

using namespace std;

namespace rw {

uint32 index;

void Geometry::readPs2NativeSkin(ifstream &rw)
{
	HeaderInfo header;

	READ_HEADER(CHUNK_STRUCT);

	if (readUInt32(rw) != PLATFORM_PS2) {
		cerr << "error: native data not in ps2 format\n";
		return;
	}

	// don't know if correct
	boneCount = readUInt8(rw);
	specialIndexCount = readUInt8(rw);
	unknown1 = readUInt8(rw);
	unknown2 = readUInt8(rw);

	inverseMatrices.resize(boneCount*0x10);
	for (uint32 i = 0; i < boneCount; i++)
		rw.read((char *) (&inverseMatrices[i*0x10]),
		        0x10*sizeof(float32));
}

void Geometry::readPs2NativeData(ifstream &rw)
{
	HeaderInfo header;

	READ_HEADER(CHUNK_STRUCT); /* wrong size */

	if (readUInt32(rw) != PLATFORM_PS2) {
		cerr << "error: native data not in ps2 format\n";
		return;
	}
	index = 0;
	vector<uint32> typesRead;
	for (uint32 i = 0; i < splits.size(); i++) {
		uint32 splitSize = readUInt32(rw);
		rw.seekg(4, ios::cur);

		uint32 numIndices = splits[i].indices.size();
		splits[i].indices.clear();
		uint32 end = splitSize + rw.tellg();

		// chunk8 and chunk32 contain the same data !!
		uint8 chunk8[16];
		uint32 *chunk32 = (uint32 *) chunk8;

		uint32 blockStart = rw.tellg();
		bool reachedEnd;
		bool sectionALast = false;
		bool sectionBLast = false;
		bool dataAread = false;
		while (rw.tellg() < end) {
			/* sectionA */
			reachedEnd = false;
			while (!reachedEnd && !sectionALast) {
				rw.read((char *) chunk8, 0x10);
				switch (chunk8[3]) {
				case 0x30: {
					/* read all split data when we find the
					 * first data block and ignore all
					 * other blocks */
					if (dataAread) {
						/* skip dummy data */
						rw.seekg(0x10, ios::cur);
						break;
					}
					uint32 oldPos = rw.tellg();
					uint32 dataPos = blockStart +
					                   chunk32[1]*0x10;
					rw.seekg(dataPos, ios::beg);
					readData(numIndices,chunk32[3], i, rw);
					rw.seekg(oldPos + 0x10, ios::beg);
					break;
				}
				case 0x60:
					sectionALast = true;
					/* fall through */
				case 0x10:
					reachedEnd = true;
					dataAread = true;
					break;
				default:
					break;
				}
			}

			/* sectionB */
			reachedEnd = false;
			while (!reachedEnd && !sectionBLast) {
				rw.read((char *) chunk8, 0x10);
				switch (chunk8[3]) {
				case 0x00:
				case 0x07:
					readData(chunk8[14],chunk32[3], i, rw);
					/* remember what sort of data we read */
					typesRead.push_back(chunk32[3]);
					break;
				case 0x04:
					if (chunk8[7] == 0x15)
						;//first
					else if (chunk8[7] == 0x17)
						;//not first

					if ((chunk8[11] == 0x11 &&
					      chunk8[15] == 0x11) ||
					    (chunk8[11] == 0x11 &&
					      chunk8[15] == 0x06)) {
						// last
						rw.seekg(end, ios::beg);
						typesRead.clear();
						sectionBLast = true;
					} else if (chunk8[11] == 0 &&
					           chunk8[15] == 0 &&
					           faceType == FACETYPE_STRIP) {
						deleteOverlapping(typesRead, i);
						typesRead.clear();
						// not last
					}
					reachedEnd = true;
					break;
				default:
					break;
				}
			}
		}
	}
}


void Geometry::readData(uint32 vertexCount, uint32 type,
                        uint32 split, ifstream &rw)
{
	float32 vertexScale = (flags & FLAGS_NORMALS) ? VERTSCALE2 : VERTSCALE1;
	// perhaps this is the only condition?
	if (flags & FLAGS_PRELIT)
		vertexScale = VERTSCALE1;

	uint32 size;
	type &= 0xFF00FFFF;
	/* TODO: read greater chunks */
	switch (type) {
	/* Vertices */
	case 0x68008000: {
		size = 3 * sizeof(float32);
		for (uint32 j = 0; j < vertexCount; j++) {
			vertices.push_back(readFloat32(rw));
			vertices.push_back(readFloat32(rw));
			vertices.push_back(readFloat32(rw));
			splits[split].indices.push_back(index++);
		}
		break;
	} case 0x6D008000: {
		size = 4 * sizeof(int16);
		int16 vertex[4];
		for (uint32 j = 0; j < vertexCount; j++) {
			rw.read((char *) (vertex), size);
			uint32 flag = vertex[3] & 0xFFFF;
			vertices.push_back(vertex[0] * vertexScale);
			vertices.push_back(vertex[1] * vertexScale);
			vertices.push_back(vertex[2] * vertexScale);
			if (flag == 0x8000)
				splits[split].indices.push_back(index-1);
			splits[split].indices.push_back(index++);
		}
		break;
	/* Texture coordinates */
	} case 0x64008001: {
		size = 2 * sizeof(float32);
		for (uint32 j = 0; j < vertexCount; j++) {
			texCoords[0].push_back(readFloat32(rw));
			texCoords[0].push_back(readFloat32(rw));
		}
		break;
	} case 0x6D008001: {
		size = 2 * sizeof(int16);	// 2 was 4
		int16 texCoord[2];
		for (uint32 j = 0; j < vertexCount; j++) {
			/* TODO: don't know if this is correct (should be) */
			for (uint32 i = 0; i < numUVs; i++) {
				rw.read((char *) (texCoord), size);
				texCoords[i].push_back(texCoord[0] * UVSCALE);
				texCoords[i].push_back(texCoord[1] * UVSCALE);
			}
		}
		size *= numUVs;
		break;
	} case 0x65008001: {
		size = 2 * sizeof(int16);
		int16 texCoord[2];
		for (uint32 j = 0; j < vertexCount; j++) {
			rw.read((char *) (texCoord), size);
			texCoords[0].push_back(texCoord[0] * UVSCALE);
			texCoords[0].push_back(texCoord[1] * UVSCALE);
		}
		break;
	/* Vertex colors */
	} case 0x6D00C002: {
		size = 8 * sizeof(uint8);
		for (uint32 j = 0; j < vertexCount; j++) {
			vertexColors.push_back(readUInt8(rw));
			nightColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
			nightColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
			nightColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
			nightColors.push_back(readUInt8(rw));
		}
		break;
	} case 0x6E00C002: {
		size = 4 * sizeof(uint8);
		for (uint32 j = 0; j < vertexCount; j++) {
			vertexColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
			vertexColors.push_back(readUInt8(rw));
		}
		break;
	/* Normals */
	} case 0x6E008002: case 0x6E008003: {
		size = 4 * sizeof(int8);
		int8 normal[4];
		for (uint32 j = 0; j < vertexCount; j++) {
			rw.read((char *) (normal), size);
			normals.push_back(normal[0] * NORMALSCALE);
			normals.push_back(normal[1] * NORMALSCALE);
			normals.push_back(normal[2] * NORMALSCALE);
		}
		break;
	} case 0x6A008003: {
		size = 3 * sizeof(int8);
		int8 normal[3];
		for (uint32 j = 0; j < vertexCount; j++) {
			rw.read((char *) (normal), size);
			normals.push_back(normal[0] * NORMALSCALE);
			normals.push_back(normal[1] * NORMALSCALE);
			normals.push_back(normal[2] * NORMALSCALE);
		}
		break;
	/* Skin weights and indices */
	} case 0x6C008004: case 0x6C008003: case 0x6C008001: {
		size = 4 * sizeof(float32);
		float32 weight[4];
		uint32 *w = (uint32 *) weight;
		uint8 indices[4];;
		for (uint32 j = 0; j < vertexCount; j++) {
			rw.read((char *) (weight), size);
			for (uint32 i = 0; i < 4; i++) {
				vertexBoneWeights.push_back(weight[i]);
				indices[i] = w[i] >> 2;
				if (indices[i] != 0)
					indices[i] -= 1;
			}
			vertexBoneIndices.push_back(indices[3] << 24 |
			                            indices[2] << 16 |
			                            indices[1] << 8 |
			                            indices[0]);
		}
		break;
	}
	default:
		cout << "unknown data type: " << hex << type;
		cout << " " << filename << " " << hex << rw.tellg() << endl;
		break;
	}

	/* skip padding */
	if (vertexCount*size % 0x10 != 0)
		rw.seekg(0x10 - vertexCount*size % 0x10, ios::cur);
}

void Geometry::deleteOverlapping(vector<uint32> &typesRead, uint32 split)
{
	uint32 size;
	for (uint32 i = 0; i < typesRead.size(); i++) {
		switch (typesRead[i] & 0xFF00FFFF) {
		/* Vertices */
		case 0x68008000:
		case 0x6D008000:
			size = vertices.size();
			vertices.resize(size-2*3);

			size = splits[split].indices.size();
			splits[split].indices.resize(size-2);
			index -= 2;
			break;
		/* Texture coordinates */
		case 0x6D008001:
			for (uint32 j = 0; j < numUVs; j++) {
				size = texCoords[j].size();
				texCoords[j].resize(size-2*2);
			}
			break;
		case 0x64008001:
		case 0x65008001:
			size = texCoords[0].size();
			texCoords[0].resize(size-2*2);
			break;
		/* Vertex colors */
		case 0x6D00C002:
			size = nightColors.size();
			nightColors.resize(size-2*4);
			/* fall through */
		case 0x6E00C002:
			size = vertexColors.size();
			vertexColors.resize(size-2*4);
			break;
		/* Normals */
		case 0x6E008002:
		case 0x6E008003:
		case 0x6A008003:
			size = normals.size();
			normals.resize(size-2*3);
			break;
		/* Skin weights and indices*/
		case 0x6C008004:
		case 0x6C008003:
		case 0x6C008001:
			size = vertexBoneWeights.size();
			vertexBoneWeights.resize(size-2*4);

			size = vertexBoneIndices.size();
			vertexBoneIndices.resize(size-2);
			break;
		default:
			cout << "unknown data type: ";
			cout << hex <<typesRead[i] << endl;
			break;
		}
	}
}

}