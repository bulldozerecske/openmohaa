/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_marks.c -- polygon projection on the world polygons

#include "tr_local.h"
//#include "assert.h"

#define MAX_VERTS_ON_POLY		64

#define MARKER_OFFSET			0	// 1

/*
=============
R_ChopPolyBehindPlane

Out must have space for two more vertexes than in
=============
*/
#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
static void R_ChopPolyBehindPlane( int numInPoints, const vec3_t inPoints[MAX_VERTS_ON_POLY],
								   int *numOutPoints, vec3_t outPoints[MAX_VERTS_ON_POLY], 
							const vec3_t normal, vec_t dist, vec_t epsilon) {
	float		dists[MAX_VERTS_ON_POLY+4];
	int			sides[MAX_VERTS_ON_POLY+4];
	int			counts[3];
	float		dot;
	int			i, j;
	const float* p1, * p2;
	float		*clip;
	float		d;

	// don't clip if it might overflow
	if ( numInPoints >= MAX_VERTS_ON_POLY - 2 ) {
		*numOutPoints = 0;
		return;
	}

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	for ( i = 0 ; i < numInPoints ; i++ ) {
		dot = DotProduct( inPoints[i], normal );
		dot -= dist;
		dists[i] = dot;
		if ( dot > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( dot < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	*numOutPoints = 0;

	if ( !counts[0] ) {
		return;
	}
	if ( !counts[1] ) {
		*numOutPoints = numInPoints;
		Com_Memcpy( outPoints, inPoints, numInPoints * sizeof(vec3_t) );
		return;
	}

	for ( i = 0 ; i < numInPoints ; i++ ) {
		p1 = inPoints[i];
		clip = outPoints[ *numOutPoints ];
		
		if ( sides[i] == SIDE_ON ) {
			VectorCopy( p1, clip );
			(*numOutPoints)++;
			continue;
		}
	
		if ( sides[i] == SIDE_FRONT ) {
			VectorCopy( p1, clip );
			(*numOutPoints)++;
			clip = outPoints[ *numOutPoints ];
		}

		if ( sides[i+1] == SIDE_ON || sides[i+1] == sides[i] ) {
			continue;
		}
			
		// generate a split point
		p2 = inPoints[ (i+1) % numInPoints ];

		d = dists[i] - dists[i+1];
		if ( d == 0 ) {
			dot = 0;
		} else {
			dot = dists[i] / d;
		}

		// clip xyz

		for (j=0 ; j<3 ; j++) {
			clip[j] = p1[j] + dot * ( p2[j] - p1[j] );
		}

		(*numOutPoints)++;
	}
}

/*
=================
R_BoxSurfaces_r

=================
*/
void R_BoxSurfaces_r(mnode_t *node, vec3_t mins, vec3_t maxs, surfaceType_t **list, int listsize, int *listlength, vec3_t dir) {

	int			s, c;
	msurface_t	*surf, **mark;

	// do the tail recursion in a loop
	while ( node->contents == -1 ) {
		s = BoxOnPlaneSide( mins, maxs, node->plane );
		if (s == 1) {
			node = node->children[0];
		} else if (s == 2) {
			node = node->children[1];
		} else {
			R_BoxSurfaces_r(node->children[0], mins, maxs, list, listsize, listlength, dir);
			node = node->children[1];
		}
	}

	// add the individual surfaces
	mark = node->firstmarksurface;
	c = node->nummarksurfaces;
	while (c--) {
		//
		if (*listlength >= listsize) break;
		//
		surf = *mark;
		// check if the surface has NOIMPACT or NOMARKS set
		if ( ( surf->shader->surfaceFlags & ( SURF_NOIMPACT | SURF_NOMARKS ) ) ) {
			surf->viewCount = tr.viewCount;
		}
		// extra check for surfaces to avoid list overflows
		else if (*(surf->data) == SF_FACE) {
			// the face plane should go through the box
			s = BoxOnPlaneSide( mins, maxs, &(( srfSurfaceFace_t * ) surf->data)->plane );
			if (s == 1 || s == 2) {
				surf->viewCount = tr.viewCount;
			} else if (DotProduct((( srfSurfaceFace_t * ) surf->data)->plane.normal, dir) > -0.5) {
			// don't add faces that make sharp angles with the projection direction
				surf->viewCount = tr.viewCount;
			}
		}
		else if (*(surfaceType_t *) (surf->data) != SF_GRID) surf->viewCount = tr.viewCount;
		// check the viewCount because the surface may have
		// already been added if it spans multiple leafs
		if (surf->viewCount != tr.viewCount) {
			surf->viewCount = tr.viewCount;
			list[*listlength] = (surfaceType_t *) surf->data;
			(*listlength)++;
		}
		mark++;
	}
}

/*
=================
R_AddMarkFragments

=================
*/
void R_AddMarkFragments(int numClipPoints, vec3_t clipPoints[2][MAX_VERTS_ON_POLY],
				   int numPlanes, const vec3_t *normals, const float *dists,
				   int maxPoints, vec3_t pointBuffer,
				   markFragment_t *fragmentBuffer,
				   int *returnedPoints, int *returnedFragments,
				   float fOnEpsilon) {
	int pingPong, i;
	markFragment_t	*mf;

	// chop the surface by all the bounding planes of the to be projected polygon
	pingPong = 0;

	for ( i = 0 ; i < numPlanes ; i++ ) {

		R_ChopPolyBehindPlane( numClipPoints, clipPoints[pingPong],
						   &numClipPoints, clipPoints[!pingPong],
							normals[i], dists[i], 0.5 );
		pingPong ^= 1;
		if ( numClipPoints == 0 ) {
			break;
		}
	}
	// completely clipped away?
	if ( numClipPoints == 0 ) {
		return;
	}

	// add this fragment to the returned list
	if ( numClipPoints + (*returnedPoints) > maxPoints ) {
		return;	// not enough space for this polygon
	}

	mf = fragmentBuffer + (*returnedFragments);
	mf->firstPoint = (*returnedPoints);
	mf->numPoints = numClipPoints;
	mf->iIndex = 0;
	Com_Memcpy( pointBuffer + (*returnedPoints) * 3, clipPoints[pingPong], numClipPoints * sizeof(vec3_t) );

	(*returnedPoints) += numClipPoints;
	(*returnedFragments)++;
}

void R_AddMarkFragmentsToTerrain(
	cTerraPatchUnpacked_t* pTerPatch,
	int numPlanes,
    const vec3_t* normals,
    float* dists,
	int maxPoints,
	vec3_t pointBuffer,
	markFragment_t* fragmentBuffer,
	int* returnedPoints,
	int* returnedFragments
)
{
	int i, j;
	int iType;
	int iFirstFragment;
	unsigned char* pubHeight;
	vec3_t v[8];
	vec3_t clipPoints[2][MAX_VERTS_ON_POLY];

	dists[numPlanes - 2] -= 96.f;
	dists[numPlanes - 1] -= 108.f;

	v[0][0] = pTerPatch->x0;
	v[0][1] = pTerPatch->y0;
	v[0][2] = pTerPatch->z0;
	v[1][0] = pTerPatch->x0;
	v[1][1] = pTerPatch->y0 + 512.f;
    v[1][2] = pTerPatch->z0;
    v[2][0] = pTerPatch->x0 + 512.f;
    v[2][1] = pTerPatch->y0 + 512.f;
    v[2][2] = pTerPatch->z0;
    v[3][0] = pTerPatch->x0 + 512.f;
    v[3][1] = pTerPatch->y0;
    v[3][2] = pTerPatch->z0;
    v[4][0] = pTerPatch->x0;
    v[4][1] = pTerPatch->y0;
    v[4][2] = pTerPatch->z0 + 510.f;
    v[5][0] = pTerPatch->x0;
    v[5][1] = pTerPatch->y0 + 512.f;
    v[5][2] = pTerPatch->z0 + 510.f;
    v[6][0] = pTerPatch->x0 + 512.f;
    v[6][1] = pTerPatch->y0 + 512.f;
    v[6][2] = pTerPatch->z0 + 510.f;
    v[7][0] = pTerPatch->x0 + 512.f;
    v[7][1] = pTerPatch->y0;
    v[7][2] = pTerPatch->z0 + 510.f;

	for (i = 0; i < numPlanes; i++) {
		pubHeight = pTerPatch->heightmap;
		iFirstFragment = *returnedFragments;

		for (j = 0; j < 8; j++) {
			if (DotProduct(v[i], normals[i]) - dists[i] > 0.f) {
				break;
			}
		}

		if (j == 8)
		{
			dists[numPlanes - 2] += 96.f;
			dists[numPlanes - 1] += 108.f;
			return;
		}
	}

	pubHeight = pTerPatch->heightmap;
	iFirstFragment = *returnedFragments;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			iType = ri.CM_TerrainSquareType(pTerPatch - tr.world->terraPatches, i, j);
			if (!iType) {
				continue;
			}

			v[0][0] = (i << 6) + pTerPatch->x0;
			v[0][1] = (j << 6) + pTerPatch->y0;
            v[0][2] = (pubHeight[0] << 1) + pTerPatch->z0;
            v[1][0] = (i << 6) + pTerPatch->x0 + 64.f;
            v[1][1] = (j << 6) + pTerPatch->y0;
            v[1][2] = (pubHeight[1] << 1) + pTerPatch->z0;
            v[2][0] = (i << 6) + pTerPatch->x0 + 64.f;
            v[2][1] = (j << 6) + pTerPatch->y0 + 64.f;
            v[2][2] = (pubHeight[10] << 1) + pTerPatch->z0;
            v[3][0] = (i << 6) + pTerPatch->x0;
            v[3][1] = (j << 6) + pTerPatch->y0 + 64.f;
            v[3][2] = (pubHeight[9] << 1) + pTerPatch->z0;

			if (((j & 0xFF) + (i & 0xFF)) & 1) {
				if (iType != 3) {
					VectorCopy(v[0], clipPoints[0][0]);
					VectorCopy(v[3], clipPoints[0][1]);
					VectorCopy(v[1], clipPoints[0][2]);

					R_AddMarkFragments(
						3,
						clipPoints,
						numPlanes,
						normals,
						dists,
						maxPoints,
						pointBuffer,
						fragmentBuffer,
						returnedPoints,
						returnedFragments,
						0.f
					);
				}

                if (iType != 6) {
                    VectorCopy(v[2], clipPoints[0][0]);
                    VectorCopy(v[1], clipPoints[0][1]);
                    VectorCopy(v[3], clipPoints[0][2]);

                    R_AddMarkFragments(
                        3,
                        clipPoints,
                        numPlanes,
                        normals,
                        dists,
                        maxPoints,
                        pointBuffer,
                        fragmentBuffer,
                        returnedPoints,
                        returnedFragments,
                        0.f
                    );
				}
			} else {
                if (iType != 5) {
                    VectorCopy(v[3], clipPoints[0][0]);
                    VectorCopy(v[2], clipPoints[0][1]);
                    VectorCopy(v[0], clipPoints[0][2]);

                    R_AddMarkFragments(
                        3,
                        clipPoints,
                        numPlanes,
                        normals,
                        dists,
                        maxPoints,
                        pointBuffer,
                        fragmentBuffer,
                        returnedPoints,
                        returnedFragments,
                        0.f
                    );
				}

                if (iType != 4) {
                    VectorCopy(v[1], clipPoints[0][0]);
                    VectorCopy(v[0], clipPoints[0][1]);
                    VectorCopy(v[2], clipPoints[0][2]);

                    R_AddMarkFragments(
                        3,
                        clipPoints,
                        numPlanes,
                        normals,
                        dists,
                        maxPoints,
                        pointBuffer,
                        fragmentBuffer,
                        returnedPoints,
                        returnedFragments,
                        0.f
                    );
				}
			}
		}
	}

	for (i = iFirstFragment; i < *returnedFragments; i++) {
		fragmentBuffer[i].iIndex = pTerPatch - tr.world->terraPatches + 1;
	}

    dists[numPlanes - 2] += 96.f;
    dists[numPlanes - 1] += 108.f;
}

void R_TessellateMarkFragments(
	int* pReturnedPoints,
	int* pReturnedFragments,
	int numsurfaces,
	surfaceType_t** surfaces,
	const vec3_t projectionDir,
	int numPlanes,
    const vec3_t* normals,
    float* dists,
	int maxPoints,
	vec3_t pointBuffer,
	int maxFragments,
	markFragment_t* fragmentBuffer,
	float fRadiusSquared
)
{
	int i, j, k, m, n;
	int* indexes;
	float* v;
	vec3_t clipPoints[2][MAX_VERTS_ON_POLY];
	vec3_t v1, v2;
	vec3_t normal;
	drawVert_t* dv;
	qboolean bSkipTerrain;
	srfGridMesh_t* cv;
	srfSurfaceFace_t* surf;
	cTerraPatchUnpacked_t* terraPatch;
	int numClipPoints;

    bSkipTerrain = ter_minMarkRadius->value * ter_minMarkRadius->value >= (long double)fRadiusSquared;

	for (i = 0; i < numsurfaces; i++) {
		if (*surfaces[i] == SF_GRID) {
			cv = (srfGridMesh_t*)surfaces[i];

			for (m = 0; m < cv->height - 1; m++) {
				for (n = 0; n < cv->width - 1; n++) {
					// We triangulate the grid and chop all triangles within
					// the bounding planes of the to be projected polygon.
					// LOD is not taken into account, not such a big deal though.
					//
					// It's probably much nicer to chop the grid itself and deal
					// with this grid as a normal SF_GRID surface so LOD will
					// be applied. However the LOD of that chopped grid must
					// be synced with the LOD of the original curve.
					// One way to do this; the chopped grid shares vertices with
					// the original curve. When LOD is applied to the original
					// curve the unused vertices are flagged. Now the chopped curve
					// should skip the flagged vertices. This still leaves the
					// problems with the vertices at the chopped grid edges.
					//
					// To avoid issues when LOD applied to "hollow curves" (like
					// the ones around many jump pads) we now just add a 2 unit
					// offset to the triangle vertices.
					// The offset is added in the vertex normal vector direction
					// so all triangles will still fit together.
					// The 2 unit offset should avoid pretty much all LOD problems.

                    numClipPoints = 3;

                    dv = cv->verts + m * cv->width + n;

                    VectorCopy(dv[0].xyz, clipPoints[0][0]);
                    VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[0].normal, clipPoints[0][0]);
                    VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
                    VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
                    VectorCopy(dv[1].xyz, clipPoints[0][2]);
                    VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[1].normal, clipPoints[0][2]);
                    // check the normal of this triangle
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
					CrossProduct(v1, v2, normal);
					VectorNormalize(normal);
                    if (DotProduct(normal, projectionDir) < -0.1) {
                        // add the fragments of this triangle
                        R_AddMarkFragments(numClipPoints, clipPoints,
                            numPlanes, normals, dists,
                            maxPoints, pointBuffer,
                            fragmentBuffer,
                            pReturnedPoints, pReturnedFragments, 0.f);

                        if (*pReturnedFragments == maxFragments) {
                            return;	// not enough space for more fragments
                        }
                    }

					VectorCopy(dv[1].xyz, clipPoints[0][0]);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[1].normal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
					VectorCopy(dv[cv->width+1].xyz, clipPoints[0][2]);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[cv->width+1].normal, clipPoints[0][2]);
                    // check the normal of this triangle
                    VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
                    VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
                    CrossProduct(v1, v2, normal);
                    VectorNormalize(normal);
                    if (DotProduct(normal, projectionDir) < -0.05) {
                        // add the fragments of this triangle
                        R_AddMarkFragments(numClipPoints, clipPoints,
                            numPlanes, normals, dists,
                            maxPoints, pointBuffer,
                            fragmentBuffer,
                            pReturnedPoints, pReturnedFragments, 0.f);

                        if (*pReturnedFragments == maxFragments) {
                            return;	// not enough space for more fragments
                        }
                    }
				}
			}
		} else if (*surfaces[i] == SF_FACE) {
            surf = (srfSurfaceFace_t*)surfaces[i];
            // check the normal of this face
            if (DotProduct(surf->plane.normal, projectionDir) > -0.5) {
                continue;
            }

            indexes = (int*)((byte*)surf + surf->ofsIndices);

            for (k = 0; k < surf->numIndices; k += 3) {
                for (j = 0; j < 3; j++) {
                    v = surf->points[indexes[k + j]];
                    VectorMA(v, MARKER_OFFSET, surf->plane.normal, clipPoints[0][j]);
                }

                // add the fragments of this face
                R_AddMarkFragments(3, clipPoints,
                    numPlanes, normals, dists,
                    maxPoints, pointBuffer,
                    fragmentBuffer,
                    pReturnedPoints, pReturnedFragments, 0.f);
                if (*pReturnedFragments == maxFragments) {
                    return;	// not enough space for more fragments
                }
            }
		} else if (*surfaces[i] == SF_TERRAIN_PATCH) {
			terraPatch = (cTerraPatchUnpacked_t*)surfaces[i];

			if (!bSkipTerrain) {
				R_AddMarkFragmentsToTerrain(
					terraPatch,
					numPlanes,
					normals,
					dists,
					maxPoints,
					pointBuffer,
					fragmentBuffer,
					pReturnedPoints,
					pReturnedFragments
				);

				if (*pReturnedFragments == maxFragments) {
					return;
				}
			}
		}
	}
}

/*
=================
R_MarkFragments

=================
*/
int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer, float fRadiusSquared ) {
    int numsurfaces;
	int i;
    surfaceType_t* surfaces[64];
    vec3_t mins, maxs;
	vec3_t boxsize;
	float radius;
	float backpush, frontpush;
	int returnedFragments;
	int returnedPoints;
	vec3_t normals[MAX_VERTS_ON_POLY + 2];
	float dists[MAX_VERTS_ON_POLY + 2];
	vec3_t projectionDir;
	vec3_t v1, v2;
	float projdir;

	//increment view count for double check prevention
	tr.viewCount++;

	//
	VectorNormalize2( projection, projectionDir );
	// find all the brushes that are to be considered
	ClearBounds( mins, maxs );
	for ( i = 0 ; i < numPoints ; i++ ) {
		vec3_t	temp;

		AddPointToBounds( points[i], mins, maxs );
		VectorAdd( points[i], projection, temp );
		AddPointToBounds( temp, mins, maxs );
		// make sure we get all the leafs (also the one(s) in front of the hit surface)
		VectorMA( points[i], -20, projectionDir, temp );
		AddPointToBounds( temp, mins, maxs );
	}

	VectorSubtract(maxs, mins, v1);
	projdir = -DotProduct(v1, projectionDir);
	VectorMA(v1, projdir, projectionDir, boxsize);
	radius = VectorLength(boxsize);

    frontpush = radius;
    if (radius >= 32.f) frontpush = 32.f;
    backpush = radius;
	if (radius >= 20.f) backpush = radius;

	if (numPoints > MAX_VERTS_ON_POLY) numPoints = MAX_VERTS_ON_POLY;
	// create the bounding planes for the to be projected polygon
	for ( i = 0 ; i < numPoints ; i++ ) {
		VectorSubtract(points[(i+1)%numPoints], points[i], v1);
		VectorAdd(points[i], projection, v2);
		VectorSubtract(points[i], v2, v2);
		CrossProduct(v1, v2, normals[i]);
		VectorNormalizeFast(normals[i]);
		dists[i] = DotProduct(normals[i], points[i]);
	}
	// add near and far clipping planes for projection
	VectorCopy(projectionDir, normals[numPoints]);
	dists[numPoints] = DotProduct(normals[numPoints], points[0]) - frontpush;
	VectorCopy(projectionDir, normals[numPoints+1]);
	VectorInverse(normals[numPoints+1]);
	dists[numPoints+1] = DotProduct(normals[numPoints+1], points[0]) - backpush;

	returnedPoints = 0;
	returnedFragments = 0;
	numsurfaces = 0;

	R_BoxSurfaces_r(tr.world->nodes, mins, maxs, surfaces, 64, &numsurfaces, projectionDir);
	if (!numsurfaces) return 0;

    R_TessellateMarkFragments(
        &returnedPoints,
        &returnedFragments,
        numsurfaces,
        surfaces,
        projectionDir,
        numPoints + 2,
        normals,
        dists,
        maxPoints,
        pointBuffer,
        maxFragments,
        fragmentBuffer,
        fRadiusSquared
	);

    return returnedFragments;

	returnedPoints = 0;
	returnedFragments = 0;

#if 0
	for ( i = 0 ; i < numsurfaces ; i++ ) {

		if (*surfaces[i] == SF_GRID) {
			cv = (srfGridMesh_t *) surfaces[i];
			for ( m = 0 ; m < cv->height - 1 ; m++ ) {
				for ( n = 0 ; n < cv->width - 1 ; n++ ) {
					// We triangulate the grid and chop all triangles within
					// the bounding planes of the to be projected polygon.
					// LOD is not taken into account, not such a big deal though.
					//
					// It's probably much nicer to chop the grid itself and deal
					// with this grid as a normal SF_GRID surface so LOD will
					// be applied. However the LOD of that chopped grid must
					// be synced with the LOD of the original curve.
					// One way to do this; the chopped grid shares vertices with
					// the original curve. When LOD is applied to the original
					// curve the unused vertices are flagged. Now the chopped curve
					// should skip the flagged vertices. This still leaves the
					// problems with the vertices at the chopped grid edges.
					//
					// To avoid issues when LOD applied to "hollow curves" (like
					// the ones around many jump pads) we now just add a 2 unit
					// offset to the triangle vertices.
					// The offset is added in the vertex normal vector direction
					// so all triangles will still fit together.
					// The 2 unit offset should avoid pretty much all LOD problems.

					numClipPoints = 3;

					dv = cv->verts + m * cv->width + n;

					VectorCopy(dv[0].xyz, clipPoints[0][0]);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[0].normal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
					VectorCopy(dv[1].xyz, clipPoints[0][2]);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[1].normal, clipPoints[0][2]);
					// check the normal of this triangle
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.1) {
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
										   numPlanes, normals, dists,
										   maxPoints, pointBuffer,
										    fragmentBuffer,
										   &returnedPoints, &returnedFragments, 0.f);

						if ( returnedFragments == maxFragments ) {
							return returnedFragments;	// not enough space for more fragments
						}
					}

					VectorCopy(dv[1].xyz, clipPoints[0][0]);
					VectorMA(clipPoints[0][0], MARKER_OFFSET, dv[1].normal, clipPoints[0][0]);
					VectorCopy(dv[cv->width].xyz, clipPoints[0][1]);
					VectorMA(clipPoints[0][1], MARKER_OFFSET, dv[cv->width].normal, clipPoints[0][1]);
					VectorCopy(dv[cv->width+1].xyz, clipPoints[0][2]);
					VectorMA(clipPoints[0][2], MARKER_OFFSET, dv[cv->width+1].normal, clipPoints[0][2]);
					// check the normal of this triangle
					VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
					VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
					CrossProduct(v1, v2, normal);
					VectorNormalizeFast(normal);
					if (DotProduct(normal, projectionDir) < -0.05) {
						// add the fragments of this triangle
						R_AddMarkFragments(numClipPoints, clipPoints,
										   numPlanes, normals, dists,
										   maxPoints, pointBuffer,
										    fragmentBuffer,
										   &returnedPoints, &returnedFragments, 0.f);

						if ( returnedFragments == maxFragments ) {
							return returnedFragments;	// not enough space for more fragments
						}
					}
				}
			}
		}
		else if (*surfaces[i] == SF_FACE) {

			surf = ( srfSurfaceFace_t * ) surfaces[i];
			// check the normal of this face
			if (DotProduct(surf->plane.normal, projectionDir) > -0.5) {
				continue;
			}

			/*
			VectorSubtract(clipPoints[0][0], clipPoints[0][1], v1);
			VectorSubtract(clipPoints[0][2], clipPoints[0][1], v2);
			CrossProduct(v1, v2, normal);
			VectorNormalize(normal);
			if (DotProduct(normal, projectionDir) > -0.5) continue;
			*/
			indexes = (int *)( (byte *)surf + surf->ofsIndices );
			for ( k = 0 ; k < surf->numIndices ; k += 3 ) {
				for ( j = 0 ; j < 3 ; j++ ) {
					v = surf->points[0] + VERTEXSIZE * indexes[k+j];;
					VectorMA( v, MARKER_OFFSET, surf->plane.normal, clipPoints[0][j] );
				}
				// add the fragments of this face
				R_AddMarkFragments( 3 , clipPoints,
								   numPlanes, normals, dists,
								   maxPoints, pointBuffer,
								   fragmentBuffer,
								   &returnedPoints, &returnedFragments, 0.f);
				if ( returnedFragments == maxFragments ) {
					return returnedFragments;	// not enough space for more fragments
				}
			}
			continue;
		}
		else {
			// ignore all other world surfaces
			// might be cool to also project polygons on a triangle soup
			// however this will probably create huge amounts of extra polys
			// even more than the projection onto curves
			continue;
		}
	}
	return returnedFragments;
#endif
}

void R_BoxSurfacesForBModel_r(bmodel_t* pBmodel, const vec3_t mins, const vec3_t maxs, surfaceType_t** list, int listsize, int* listlength, const vec3_t dir)
{
	int s;
	int c;
	msurface_t* surf;
	int i;

	surf = pBmodel->firstSurface;

	for (i = 0; i < listsize && i < pBmodel->numSurfaces; i++, surf++) {
		s = surf->shader->surfaceFlags;
		c = surf->shader->contentFlags;

		if (!(s & SURF_NOIMPACT)
			&& !(s & SURF_NOMARKS)
			&& !(c & CONTENTS_FOG))
		{
			switch (*surf->data)
			{
			case SF_FACE:
            {
                int plane;
                srfSurfaceFace_t* face;

				face = (srfSurfaceFace_t*)surf->data;
				plane = BoxOnPlaneSide(mins, maxs, &face->plane);
				if (plane == PLANE_Y || plane == PLANE_Z) {
					surf->viewCount = tr.viewCount;
				}
				else if (DotProduct(dir, face->plane.normal) > 0.5f) {
					surf->viewCount = tr.viewCount;
				}
				break;
			}
            case SF_GRID:
                surf->viewCount = tr.viewCount;
				break;
			default:
				break;
			}
		}
		else
		{
			surf->viewCount = tr.viewCount;
		}

		if (surf->viewCount != tr.viewCount)
		{
			surf->viewCount = tr.viewCount;
			list[(*listlength)++] = surf->data;
		}
	}
}

int R_MarkFragmentsForInlineModel(clipHandle_t bmodel, const vec3_t vAngles, const vec3_t vOrigin, int numPoints,
    const vec3_t* points, const vec3_t projection, int maxPoints, vec3_t pointBuffer,
    int maxFragments, markFragment_t* fragmentBuffer, float fRadiusSquared)
{
	int i;
	int numsurfaces;
	vec3_t vTmp;
	vec3_t v1, v2;
	vec3_t vMins, vMaxs;
	vec3_t vTransPoints[64];
	vec3_t vTransProj;
	vec3_t vTransProjDir;
	bmodel_t* pBmodel;
	surfaceType_t* surfaces[64];
	int returnedFragments;
	int returnedPoints;
	vec3_t normals[MAX_VERTS_ON_POLY+2];
	float dists[MAX_VERTS_ON_POLY+2];

    tr.viewCount++;

    pBmodel = &tr.world->bmodels[bmodel];

	if (numPoints > 64) {
		numPoints = 64;
	}

	if (vAngles[0] || vAngles[1] || vAngles[2])
	{
		vec3_t axis[3];

		AngleVectorsLeft(vAngles, axis[0], axis[1], axis[2]);
	
		for (i = 0; i < numPoints; i++) {
			VectorSubtract(points[i], vOrigin, vTmp);
			MatrixTransformVectorRight(axis, vTmp, vTransPoints[i]);
		}

		MatrixTransformVectorRight(axis, projection, vTransProj);
	}
	else
	{
        for (i = 0; i < numPoints; i++) {
            VectorSubtract(points[i], vOrigin, vTransPoints[i]);
		}

		VectorCopy(projection, vTransProj);
	}

    VectorNormalize2(vTransProj, vTransProjDir);
    ClearBounds(vMins, vMaxs);

	for (i = 0; i < numPoints; i++) {
		AddPointToBounds(vTransPoints[i], vMins, vMaxs);
		VectorAdd(vTransPoints[i], vTransProj, vTmp);
		AddPointToBounds(vTmp, vMins, vMaxs);
		VectorMA(vTransPoints[i], -20.f, vTransProjDir, vTmp);
		AddPointToBounds(vTmp, vMins, vMaxs);
	}

	for (i = 0; i < numPoints; i++) {
		VectorSubtract(vTransPoints[(i + 1) % numPoints], vTransPoints[i], v1);
		VectorAdd(vTransPoints[i], vTransProj, v2);
		VectorSubtract(vTransPoints[i], v2, v2);
		CrossProduct(v1, v2, normals[i]);
		VectorNormalize(normals[i]);
		dists[i] = DotProduct(normals[i], vTransPoints[i]);
	}

	VectorCopy(vTransProjDir, normals[numPoints]);
	dists[numPoints] = DotProduct(normals[numPoints], vTransPoints[0]) - 32.f;

	VectorCopy(vTransProjDir, normals[numPoints + 1]);
	VectorInverse(normals[numPoints + 1]);
	dists[numPoints + 1] = DotProduct(normals[numPoints + 1], vTransPoints[0]) - 20.f;

	returnedPoints = 0;
	returnedFragments = 0;
	numsurfaces = 0;

	R_BoxSurfacesForBModel_r(pBmodel, vMins, vMaxs, surfaces, ARRAY_LEN(surfaces), &numsurfaces, vTransProjDir);
	if (!numsurfaces) {
		return 0;
	}

    R_TessellateMarkFragments(
        &returnedPoints,
        &returnedFragments,
        numsurfaces,
        surfaces,
        vTransProjDir,
        numPoints + 2,
        normals,
        dists,
        maxPoints,
        pointBuffer,
        maxFragments,
        fragmentBuffer,
        fRadiusSquared
	);

    return returnedFragments;
}