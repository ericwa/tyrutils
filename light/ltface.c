/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <light/light.h>
#include <light/entities.h>

extern vec3_t *vertex_normals;
extern unsigned int lightturb;

/* ======================================================================== */

typedef struct {
    vec3_t data[3];     /* permuted 3x3 matrix */
    int row[3];         /* row permutations */
    int col[3];         /* column permutations */
} pmatrix3_t;

/*
 * To do arbitrary transformation of texture coordinates to world
 * coordinates requires solving for three simultaneous equations. We
 * set up the LU decomposed form of the transform matrix here.
 */
#define ZERO_EPSILON (0.001)
static qboolean
PMatrix3_LU_Decompose(pmatrix3_t *matrix)
{
    int i, j, k, tmp;
    vec_t max;
    int max_r, max_c;

    /* Do gauss elimination */
    for (i = 0; i < 3; ++i) {
        max = 0;
        max_r = max_c = i;
        for (j = i; j < 3; ++j) {
            for (k = i; k < 3; ++k) {
                if (fabs(matrix->data[j][k]) > max) {
                    max = fabs(matrix->data[j][k]);
                    max_r = j;
                    max_c = k;
                }
            }
        }

        /* Check for parallel planes */
        if (max < ZERO_EPSILON)
            return false;

        /* Swap rows/columns if necessary */
        if (max_r != i) {
            for (j = 0; j < 3; ++j) {
                max = matrix->data[i][j];
                matrix->data[i][j] = matrix->data[max_r][j];
                matrix->data[max_r][j] = max;
            }
            tmp = matrix->row[i];
            matrix->row[i] = matrix->row[max_r];
            matrix->row[max_r] = tmp;
        }
        if (max_c != i) {
            for (j = 0; j < 3; ++j) {
                max = matrix->data[j][i];
                matrix->data[j][i] = matrix->data[j][max_c];
                matrix->data[j][max_c] = max;
            }
            tmp = matrix->col[i];
            matrix->col[i] = matrix->col[max_c];
            matrix->col[max_c] = tmp;
        }

        /* Do pivot */
        for (j = i + 1; j < 3; ++j) {
            matrix->data[j][i] /= matrix->data[i][i];
            for (k = i + 1; k < 3; ++k)
                matrix->data[j][k] -= matrix->data[j][i] * matrix->data[i][k];
        }
    }

    return true;
}

static void
Solve3(const pmatrix3_t *matrix, const vec3_t rhs, vec3_t out)
{
    /* Use local short names just for readability (should optimize away) */
    const vec3_t *data = matrix->data;
    const int *r = matrix->row;
    const int *c = matrix->col;
    vec3_t tmp;

    /* forward-substitution */
    tmp[0] = rhs[r[0]];
    tmp[1] = rhs[r[1]] - data[1][0] * tmp[0];
    tmp[2] = rhs[r[2]] - data[2][0] * tmp[0] - data[2][1] * tmp[1];

    /* back-substitution */
    out[c[2]] = tmp[2] / data[2][2];
    out[c[1]] = (tmp[1] - data[1][2] * out[c[2]]) / data[1][1];
    out[c[0]] = (tmp[0] - data[0][1] * out[c[1]] - data[0][2] * out[c[2]])
        / data[0][0];
}

/*
 * ============================================================================
 * SAMPLE POINT DETERMINATION
 * void SetupBlock (bsp2_dface_t *f) Returns with surfpt[] set
 *
 * This is a little tricky because the lightmap covers more area than the face.
 * If done in the straightforward fashion, some of the sample points will be
 * inside walls or on the other side of walls, causing false shadows and light
 * bleeds.
 *
 * To solve this, I only consider a sample point valid if a line can be drawn
 * between it and the exact midpoint of the face.  If invalid, it is adjusted
 * towards the center until it is valid.
 *
 * FIXME: This doesn't completely work; I think what we really want is to move
 *        the light point to the nearst sample point that is on the polygon;
 * ============================================================================
 */

typedef struct {
    pmatrix3_t transform;
    const texinfo_t *texinfo;
    vec_t planedist;
} texorg_t;

/*
 * Functions to aid in calculation of polygon centroid
 */
static void
TriCentroid(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2,
            vec3_t out)
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = (v0->point[i] + v1->point[i] + v2->point[i]) / 3.0;
}

static vec_t
TriArea(const dvertex_t *v0, const dvertex_t *v1, const dvertex_t *v2)
{
    int i;
    vec3_t edge0, edge1, cross;

    for (i =0; i < 3; i++) {
        edge0[i] = v1->point[i] - v0->point[i];
        edge1[i] = v2->point[i] - v0->point[i];
    }
    CrossProduct(edge0, edge1, cross);

    return VectorLength(cross) * 0.5;
}

static void
FaceCentroid(const bsp2_dface_t *face, const bsp2_t *bsp, vec3_t out)
{
    int i, edgenum;
    dvertex_t *v0, *v1, *v2;
    vec3_t centroid, poly_centroid;
    vec_t area, poly_area;

    VectorCopy(vec3_origin, poly_centroid);
    poly_area = 0;

    edgenum = bsp->dsurfedges[face->firstedge];
    if (edgenum >= 0)
        v0 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
    else
        v0 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];

    for (i = 1; i < face->numedges - 1; i++) {
        edgenum = bsp->dsurfedges[face->firstedge + i];
        if (edgenum >= 0) {
            v1 = bsp->dvertexes + bsp->dedges[edgenum].v[0];
            v2 = bsp->dvertexes + bsp->dedges[edgenum].v[1];
        } else {
            v1 = bsp->dvertexes + bsp->dedges[-edgenum].v[1];
            v2 = bsp->dvertexes + bsp->dedges[-edgenum].v[0];
        }

        area = TriArea(v0, v1, v2);
        poly_area += area;

        TriCentroid(v0, v1, v2, centroid);
        VectorMA(poly_centroid, area, centroid, poly_centroid);
    }

    VectorScale(poly_centroid, 1.0 / poly_area, out);
}

static void
PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp);

/*
 * ================
 * CreateFaceTransform
 * Fills in the transform matrix for converting tex coord <-> world coord
 * ================
 */
static void
CreateFaceTransform(const bsp2_dface_t *face, const bsp2_t *bsp,
                    pmatrix3_t *transform)
{
    const dplane_t *plane;
    const texinfo_t *tex;
    int i;

    /* Prepare the transform matrix and init row/column permutations */
    plane = &bsp->dplanes[face->planenum];
    tex = &bsp->texinfo[face->texinfo];
    for (i = 0; i < 3; i++) {
        transform->data[0][i] = tex->vecs[0][i];
        transform->data[1][i] = tex->vecs[1][i];
        transform->data[2][i] = plane->normal[i];
        transform->row[i] = transform->col[i] = i;
    }
    if (face->side)
        VectorSubtract(vec3_origin, transform->data[2], transform->data[2]);

    /* Decompose the matrix. If we can't, texture axes are invalid. */
    if (!PMatrix3_LU_Decompose(transform)) {
        logprint("Bad texture axes on face:\n");
        PrintFaceInfo(face, bsp);
        Error("CreateFaceTransform");
    }
}

static void
TexCoordToWorld(vec_t s, vec_t t, const texorg_t *texorg, vec3_t world)
{
    vec3_t rhs;

    rhs[0] = s - texorg->texinfo->vecs[0][3];
    rhs[1] = t - texorg->texinfo->vecs[1][3];
    // FIXME: This could be more or less than one unit in world space?
    rhs[2] = texorg->planedist + 1; /* one "unit" in front of surface */

    Solve3(&texorg->transform, rhs, world);
}

void
WorldToTexCoord(const vec3_t world, const texinfo_t *tex, vec_t coord[2])
{
    int i;

    /*
     * The (long double) casts below are important: The original code
     * was written for x87 floating-point which uses 80-bit floats for
     * intermediate calculations. But if you compile it without the
     * casts for modern x86_64, the compiler will round each
     * intermediate result to a 32-bit float, which introduces extra
     * rounding error.
     *
     * This becomes a problem if the rounding error causes the light
     * utilities and the engine to disagree about the lightmap size
     * for some surfaces.
     *
     * Casting to (long double) keeps the intermediate values at at
     * least 64 bits of precision, probably 128.
     */
    for (i = 0; i < 2; i++)
        coord[i] =
            (long double)world[0] * tex->vecs[i][0] +
            (long double)world[1] * tex->vecs[i][1] +
            (long double)world[2] * tex->vecs[i][2] +
                                    tex->vecs[i][3];
}

/* Debug helper - move elsewhere? */
static void
PrintFaceInfo(const bsp2_dface_t *face, const bsp2_t *bsp)
{
    const texinfo_t *tex = &bsp->texinfo[face->texinfo];
    const int offset = bsp->dtexdata.header->dataofs[tex->miptex];
    const miptex_t *miptex = (const miptex_t *)(bsp->dtexdata.base + offset);
    int i;

    logprint("face %d, texture %s, %d edges...\n"
             "  vectors (%3.3f, %3.3f, %3.3f) (%3.3f)\n"
             "          (%3.3f, %3.3f, %3.3f) (%3.3f)\n",
             (int)(face - bsp->dfaces), miptex->name, face->numedges,
             tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3],
             tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]);

    for (i = 0; i < face->numedges; i++) {
        int edge = bsp->dsurfedges[face->firstedge + i];
        int vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
        const float *point = bsp->dvertexes[vert].point;

        logprint("%s %3d (%3.3f, %3.3f, %3.3f) :: edge %d\n",
                 i ? "          " : "    verts ", vert,
                 point[0], point[1], point[2], edge);
    }
}

/*
 * ================
 * CalcFaceExtents
 * Fills in surf->texmins[], surf->texsize[] and sets surf->exactmid[]
 * ================
 */
__attribute__((noinline))
static void
CalcFaceExtents(const bsp2_dface_t *face,
                const bsp2_t *bsp, lightsurf_t *surf)
{
    vec_t mins[2], maxs[2], texcoord[2];
    vec3_t worldpoint, worldmaxs, worldmins;
    int i, j, edge, vert;
    const dvertex_t *dvertex;
    const texinfo_t *tex;

    mins[0] = mins[1] = VECT_MAX;
    maxs[0] = maxs[1] = -VECT_MAX;
    worldmaxs[0] = worldmaxs[1] = worldmaxs[2] = -VECT_MAX;
    worldmins[0] = worldmins[1] = worldmins[2] = VECT_MAX;
    tex = &bsp->texinfo[face->texinfo];

    for (i = 0; i < face->numedges; i++) {
        edge = bsp->dsurfedges[face->firstedge + i];
        vert = (edge >= 0) ? bsp->dedges[edge].v[0] : bsp->dedges[-edge].v[1];
        dvertex = &bsp->dvertexes[vert];

        VectorCopy(dvertex->point, worldpoint);
        WorldToTexCoord(worldpoint, tex, texcoord);
        for (j = 0; j < 2; j++) {
            if (texcoord[j] < mins[j])
                mins[j] = texcoord[j];
            if (texcoord[j] > maxs[j])
                maxs[j] = texcoord[j];
        }
        
        //ericw -- also save worldmaxs/worldmins, for calculating a bounding sphere
        for (j = 0; j < 3; j++) {
            if (worldpoint[j] > worldmaxs[j])
                worldmaxs[j] = worldpoint[j];
            if (worldpoint[j] < worldmins[j])
                worldmins[j] = worldpoint[j];
        }
    }

    FaceCentroid(face, bsp, worldpoint);
    WorldToTexCoord(worldpoint, tex, surf->exactmid);
    
    // calculate a bounding sphere for the face
    {
        vec3_t radius;
        
        VectorSubtract(worldmaxs, worldmins, radius);
        VectorScale(radius, 0.5, radius);
        
        VectorAdd(worldmins, radius, surf->origin);
        surf->radius = VectorLength(radius);
    }
    
    for (i = 0; i < 2; i++) {
        mins[i] = floor(mins[i] / surf->lightmapscale);
        maxs[i] = ceil(maxs[i] / surf->lightmapscale);
        surf->texmins[i] = mins[i];
        surf->texsize[i] = maxs[i] - mins[i];
        if (surf->texsize[i] >= MAXDIMENSION) {
            const dplane_t *plane = bsp->dplanes + face->planenum;
            const int offset = bsp->dtexdata.header->dataofs[tex->miptex];
            const miptex_t *miptex = (const miptex_t *)(bsp->dtexdata.base + offset);
            Error("Bad surface extents:\n"              
                  "   surface %d, %s extents = %d, scale = %g\n"
                  "   texture %s at (%s)\n"
                  "   surface normal (%s)\n",
                  (int)(face - bsp->dfaces), i ? "t" : "s", surf->texsize[i], surf->lightmapscale,
                  miptex->name, VecStr(worldpoint), VecStrf(plane->normal));
        }
    }
}

/*
 * Print warning for CalcPoint where the midpoint of a polygon, one
 * unit above the surface is covered by a solid brush.
 */
static void
WarnBadMidpoint(const vec3_t point)
{
#if 0
    static qboolean warned = false;

    if (warned)
        return;

    warned = true;
    logprint("WARNING: unable to lightmap surface near (%s)\n"
             "   This is usually caused by an unintentional tiny gap between\n"
             "   two solid brushes which doesn't leave enough room for the\n"
             "   lightmap to fit (one world unit). Further instances of this\n"
             "   warning during this compile will be supressed.\n",
             VecStr(point));
#endif
}


/* small helper that just retrieves the correct vertex from face->surfedge->edge lookups */
static int GetSurfaceVertex(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
{
        int edge = f->firstedge + v;
        edge = bsp->dsurfedges[edge];
        if (edge < 0)
                return bsp->dedges[-edge].v[1];
        return bsp->dedges[edge].v[0];
}
vec_t *GetSurfaceVertexPoint(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
{
        return bsp->dvertexes[GetSurfaceVertex(bsp, f, v)].point;
}
vec_t *GetSurfaceVertexNormal(const bsp2_t *bsp, const bsp2_dface_t *f, int v)
{
        return vertex_normals[GetSurfaceVertex(bsp, f, v)];
}


static int ClipPointToTriangle(const vec_t *orig, vec_t *point, const vec_t *norm_, const vec_t *v1, const vec_t *v2, const vec_t *v3)
{
    vec3_t d1, d2;
    float dist;

    vec3_t norm;
    /*wastefully calculate the normal*/
    VectorSubtract(v1, v2, d1);
    VectorSubtract(v3, v2, d2);
    CrossProduct(d2, d1, norm);

//VectorCopy(norm_, norm);
    VectorNormalize(norm);

    if (!norm[0] && !norm[1] && !norm[2])
        return 0;       //degenerate

    dist = DotProduct(orig, norm) - DotProduct(v1, norm);
    VectorMA(orig, -dist, norm, point);

    VectorSubtract(v1, v2, d1);
    CrossProduct(d1, norm, d2);
    VectorNormalize(d2);
    dist = DotProduct(point, d2) - DotProduct(v1, d2);
    if (dist < 0)
        VectorMA(point, -dist, d2, point);

    VectorSubtract(v2, v3, d1);
    CrossProduct(d1, norm, d2);
    VectorNormalize(d2);
    dist = DotProduct(point, d2) - DotProduct(v2, d2);
    if (dist < 0)
        VectorMA(point, -dist, d2, point);

    VectorSubtract(v3, v1, d1);
    CrossProduct(d1, norm, d2);
    VectorNormalize(d2);
    dist = DotProduct(point, d2) - DotProduct(v3, d2);
    if (dist < 0)
        VectorMA(point, -dist, d2, point);

    return 1;
}

static vec_t
TriangleArea(const vec3_t v0, const vec3_t v1, const vec3_t v2)
{
    vec3_t edge0, edge1, cross;
    VectorSubtract(v2, v0, edge0);
    VectorSubtract(v1, v0, edge1);
    CrossProduct(edge0, edge1, cross);
    
    return VectorLength(cross) * 0.5;
}


static void CalcBarycentric(const vec_t *p, const vec_t *a, const vec_t *b, const vec_t *c, vec_t *res)
{
    vec3_t v0,v1,v2;
    VectorSubtract(b, a, v0);
    VectorSubtract(c, a, v1);
    VectorSubtract(p, a, v2);
    float d00 = DotProduct(v0, v0);
    float d01 = DotProduct(v0, v1);
    float d11 = DotProduct(v1, v1);
    float d20 = DotProduct(v2, v0);
    float d21 = DotProduct(v2, v1);
    float invDenom = (d00 * d11 - d01 * d01);
    invDenom = 1.0/invDenom;
    res[1] = (d11 * d20 - d01 * d21) * invDenom;
    res[2] = (d00 * d21 - d01 * d20) * invDenom;
    res[0] = 1.0f - res[1] - res[2];
}

static void CalcPointNormal(const bsp2_t *bsp, const bsp2_dface_t *face, plane_t surfplane, vec_t *norm, const vec_t *point)
{
    // project `point` onto the surface plane (it's hovering 1 unit above)
    vec3_t pointOnPlane;
    {
        vec_t dist = DotProduct(point, surfplane.normal) - surfplane.dist;
        VectorMA(point, -dist, surfplane.normal, pointOnPlane);
    }
    
#if 1
    int j;
    vec_t *v1, *v2, *v3;
    int best; //3rd point
    vec3_t clipped, t;
//      vec3_t bestp = {point[0],point[1],point[2]};
    vec3_t barry;
    vec_t bestcost = INFINITY, cost;

    /* now just walk around the surface as a triangle fan */
    v1 = GetSurfaceVertexPoint(bsp, face, 0);
    v2 = GetSurfaceVertexPoint(bsp, face, 1);
    for (best = 0,j = 2; j < face->numedges; j++)
    {
        v3 = GetSurfaceVertexPoint(bsp, face, j);
        
        vec_t triarea = TriangleArea(v1, v2, v3);
        
        if (ClipPointToTriangle(pointOnPlane, clipped, surfplane.normal, v1, v2, v3))
        {
            VectorSubtract(clipped, pointOnPlane, t);
            cost = VectorLength(t);
            if (cost < bestcost && triarea >= 1)
            {
                best = j;
                bestcost = cost;
//                              VectorCopy(clipped, bestp);
                if (!cost)      //looks like we're already inside this triangle.
                    break;
            }
        }
        v2 = v3;
    }
#if 0
    norm[0] = (best & 1)?1:-1;
    norm[1] = (best & 2)?1:-1;
    norm[2] = (best & 4)?1:-1;
    return;
#else
    v1 = GetSurfaceVertexPoint(bsp, face, 0);
    v2 = GetSurfaceVertexPoint(bsp, face, best-1);
    v3 = GetSurfaceVertexPoint(bsp, face, best);
    
    if (TriangleArea(v1, v2, v3) < 1) {
        VectorCopy(surfplane.normal, norm);
        return;
    }
    
    CalcBarycentric(pointOnPlane, v1, v2, v3, barry);

    v1 = GetSurfaceVertexNormal(bsp, face, 0);
    v2 = GetSurfaceVertexNormal(bsp, face, best-1);
    v3 = GetSurfaceVertexNormal(bsp, face, best);
    VectorScale(v1, barry[0], norm);
    VectorMA(norm, barry[1], v2, norm);
    VectorMA(norm, barry[2], v3, norm);
#endif
#else
    /*utterly crap, just for testing. just grab closest vertex*/
    int i;
    float dist, bestd;
    int v, bestv;
    vec3_t t;

    bestv = GetSurfaceVertex(bsp, face, 0);
    VectorSubtract(point, bsp->dvertexes[bestv].point, t);
    bestd = VectorLength(t);
    for (i = 1; i < face->numedges; i++)
    {
        v = GetSurfaceVertex(bsp, face, i);
        VectorSubtract(point, bsp->dvertexes[v].point, t);
        dist = VectorLength(t);
        if (dist < bestd)
        {
            bestd = dist;
            bestv = v;
        }
    }
    VectorCopy(vertex_normals[bestv], norm);
//  VectorMA(norm, frac, t, norm);
#endif
//norm[0] = norm[1] = norm[2] = 0;
//norm[2] = 1;
    VectorNormalize(norm);
}

/*
 * =================
 * NearWall
 * 
 * returns true if any of the 6 points up/down/left/right/front/back
 * within 0.1 units of 'point' are in CONTENTS_SOLID
 * =================
 */
bool NearWall(const vec3_t point)
{
    int i;
    int insolid = 0;
    
    for (i = 0; i < 6; i++) {
        vec3_t testpoint;
        VectorCopy(point, testpoint);
        
        int axis = i/2;
        bool add = i%2;
        testpoint[axis] += (add ? 0.1 : -0.1);
        
        if (Light_PointContents(testpoint) == CONTENTS_SOLID) {
            insolid++;
        }
    }
    
    return insolid > 0;
}

/*
 * =================
 * CalcPoints
 * For each texture aligned grid point, back project onto the plane
 * to get the world xyz value of the sample point
 * =================
 */
__attribute__((noinline))
static void
CalcPoints(const dmodel_t *model, const vec3_t offset, const texorg_t *texorg, lightsurf_t *surf, const bsp2_t *bsp, const bsp2_dface_t *face)
{
    int i;
    int s, t;
    int width, height;
    vec_t step;
    vec_t starts, startt, us, ut;
    vec_t *point, *norm;
    vec3_t midpoint, move;

    /*
     * Fill in the surface points. The points are biased towards the center of
     * the surface to help avoid edge cases just inside walls
     */
    TexCoordToWorld(surf->exactmid[0], surf->exactmid[1], texorg, midpoint);
    VectorAdd(midpoint, offset, midpoint);

    width  = (surf->texsize[0] + 1) * oversample;
    height = (surf->texsize[1] + 1) * oversample;
    starts = (surf->texmins[0] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    startt = (surf->texmins[1] - 0.5 + (0.5 / oversample)) * surf->lightmapscale;
    step = surf->lightmapscale / oversample;

    /* Allocate surf->points */
    surf->numpoints = width * height;
    surf->points = calloc(surf->numpoints, sizeof(vec3_t));
    surf->normals = calloc(surf->numpoints, sizeof(vec3_t));
    point = surf->points[0];
    norm = surf->normals[0];
    for (t = 0; t < height; t++) {
        for (s = 0; s < width; s++, point += 3, norm += 3) {
            us = starts + s * step;
            ut = startt + t * step;

            TexCoordToWorld(us, ut, texorg, point);
            VectorAdd(point, offset, point);
            
            if (surf->curved)
            {
                CalcPointNormal(bsp, face, surf->plane, norm, point);
            }
            else
            {
                VectorCopy(surf->plane.normal, norm);
            }

            for (i = 0; i < 6; i++) {
                const int flags = TRACE_HIT_SOLID;
                tracepoint_t hit;
                int result;
                vec_t dist;

                result = TraceLine(model, flags, midpoint, point, &hit);
                if (result == TRACE_HIT_NONE)
                    break;
                if (result != TRACE_HIT_SOLID) {
                    WarnBadMidpoint(midpoint);
                    break;
                }

                /* Move the point 1 unit above the obstructing surface */
                dist = DotProduct(point, hit.dplane->normal) - hit.dplane->dist;
                dist = hit.side ? -dist - 1 : -dist + 1;
                VectorScale(hit.dplane->normal, dist, move);
                VectorAdd(point, move, point);
            }
        }
    }
}

__attribute__((noinline))
static void
Lightsurf_Init(const modelinfo_t *modelinfo, const bsp2_dface_t *face,
               const bsp2_t *bsp, lightsurf_t *lightsurf, facesup_t *facesup)
{
    plane_t *plane;
    const texinfo_t *tex;
    vec3_t planepoint;
    texorg_t texorg;

        /*FIXME: memset can be slow on large datasets*/
//    memset(lightsurf, 0, sizeof(*lightsurf));
    lightsurf->modelinfo = modelinfo;

    if (facesup)
        lightsurf->lightmapscale = facesup->lmscale;
    else
        lightsurf->lightmapscale = modelinfo->lightmapscale;

    if (bsp->texinfo[face->texinfo].flags & TEX_CURVED)
        lightsurf->curved = true;
    else
        lightsurf->curved = false;

    /* Set up the plane, including model offset */
    plane = &lightsurf->plane;
    VectorCopy(bsp->dplanes[face->planenum].normal, plane->normal);
    plane->dist = bsp->dplanes[face->planenum].dist;
    VectorScale(plane->normal, plane->dist, planepoint);
    VectorAdd(planepoint, modelinfo->offset, planepoint);
    plane->dist = DotProduct(plane->normal, planepoint);
    if (face->side) {
        VectorSubtract(vec3_origin, plane->normal, plane->normal);
        plane->dist = -plane->dist;
    }

    /* Set up the texorg for coordinate transformation */
    CreateFaceTransform(face, bsp, &texorg.transform);
    texorg.texinfo = &bsp->texinfo[face->texinfo];
    texorg.planedist = plane->dist;

    tex = &bsp->texinfo[face->texinfo];
    VectorCopy(tex->vecs[0], lightsurf->snormal);
    VectorSubtract(vec3_origin, tex->vecs[1], lightsurf->tnormal);
    VectorNormalize(lightsurf->snormal);
    VectorNormalize(lightsurf->tnormal);

    /* Set up the surface points */
    CalcFaceExtents(face, bsp, lightsurf);
    CalcPoints(modelinfo->model, modelinfo->offset, &texorg, lightsurf, bsp, face);
    
    /* Correct the plane for the model offset (must be done last, 
       calculation of face extents / points needs the uncorrected plane) */
    VectorScale(plane->normal, plane->dist, planepoint);
    VectorAdd(planepoint, modelinfo->offset, planepoint);
    plane->dist = DotProduct(plane->normal, planepoint);
    
    /* Correct bounding sphere */
    VectorAdd(lightsurf->origin, modelinfo->offset, lightsurf->origin);
    
    /* Allocate occlusion array */
    lightsurf->occlusion = calloc(lightsurf->numpoints, sizeof(float));
}

static void
Lightmaps_Init(const lightsurf_t *lightsurf, lightmap_t *lightmaps, const int count)
{
    int i;

    /*these are cleared on demand, there's no point clearing them twice. most of these are unused anyway,
     memset(lightmaps, 0, sizeof(lightmap_t) * count); */

    for (i = 0; i < count; i++)
    {
        lightmaps[i].style = 255;
        lightmaps[i].samples = NULL;
    }
}

/*
 * Lightmap_ForStyle
 *
 * If lightmap with given style has already been allocated, return it.
 * Otherwise, return the next available map.  A new map is not marked as
 * allocated since it may not be kept if no lights hit.
 */
static lightmap_t *
Lightmap_ForStyle(lightmap_t *lightmaps, const int style, const lightsurf_t *lightsurf)
{
    lightmap_t *lightmap = lightmaps;
    int i;

    for (i = 0; i < MAXLIGHTMAPS; i++, lightmap++) {
        if (lightmap->style == style)
            return lightmap;
        if (lightmap->style == 255)
            break;
    }

    if (lightmap->samples == NULL) {
        /* first use of this lightmap, allocate the storage for it. */
        lightmap->samples = calloc(lightsurf->numpoints, sizeof(lightsample_t));
    } else {
        /* clear only the data that is going to be merged to it. there's no point clearing more */
        memset(lightmap->samples, 0, sizeof(*lightmap->samples)*lightsurf->numpoints);
    }
    lightmap->style = 255;

    return lightmap;
}

/*
 * Lightmap_Save
 *
 * As long as we have space for the style, mark as allocated,
 * otherwise emit a warning.
 */
static void
Lightmap_Save(lightmap_t *lightmaps, const lightsurf_t *lightsurf,
              lightmap_t *lightmap, const int style)
{
    if (lightmap - lightmaps < MAXLIGHTMAPS) {
        if (lightmap->style == 255)
            lightmap->style = style;
        return;
    }

    logprint("WARNING: Too many light styles on a face\n"
             "         lightmap point near (%s)\n",
             VecStr(lightsurf->points[0]));
}

/*
 * Average adjacent points on the grid to soften shadow edges
 */
__attribute__((noinline))
static void
Lightmap_Soften(lightmap_t *lightmap, const lightsurf_t *lightsurf)
{
    int i, samples;
    int s, t, starts, startt, ends, endt;
    const lightsample_t *src;
    lightsample_t *dst;
    lightsample_t *softmap;

    const int width = (lightsurf->texsize[0] + 1) * oversample;
    const int height = (lightsurf->texsize[1] + 1) * oversample;
    const int fullsamples = (2 * softsamples + 1) * (2 * softsamples + 1);

    softmap = calloc(lightsurf->numpoints, sizeof(lightsample_t));
    
    dst = softmap;
    for (i = 0; i < lightsurf->numpoints; i++, dst++) {
        startt = qmax((i / width) - softsamples, 0);
        endt = qmin((i / width) + softsamples + 1, height);
        starts = qmax((i % width) - softsamples, 0);
        ends = qmin((i % width) + softsamples + 1, width);

        for (t = startt; t < endt; t++) {
            src = &lightmap->samples[t * width + starts];
            for (s = starts; s < ends; s++) {
                dst->light += src->light;
                VectorAdd(dst->color, src->color, dst->color);
                VectorAdd(dst->direction, src->direction, dst->direction);
                src++;
            }
        }
        /*
         * For cases where we are softening near the edge of the lightmap,
         * take extra samples from the centre point (follows old bjp tools
         * behaviour)
         */
        samples = (endt - startt) * (ends - starts);
        if (samples < fullsamples) {
            const int extraweight = 2 * (fullsamples - samples);
            src = &lightmap->samples[i];
            dst->light += src->light * extraweight;
            VectorMA(dst->color, extraweight, src->color, dst->color);
            VectorMA(dst->direction, extraweight, src->direction, dst->direction);
            samples += extraweight;
        }
        dst->light /= samples;
        VectorScale(dst->color, 1.0 / samples, dst->color);
        VectorScale(dst->direction, 1.0 / samples, dst->direction);
    }

    memcpy(lightmap->samples, softmap, lightsurf->numpoints * sizeof(lightsample_t));
    free(softmap);
}



/*
 * ============================================================================
 * FACE LIGHTING
 * ============================================================================
 */

static vec_t
GetLightValue(const lightsample_t *light, const entity_t *entity, vec_t dist)
{
    vec_t value;

    if (entity->formula == LF_INFINITE || entity->formula == LF_LOCALMIN)
        return light->light;

    value = scaledist * entity->atten * dist;
    switch (entity->formula) {
    case LF_INVERSE:
        return light->light / (value / LF_SCALE);
    case LF_INVERSE2A:
        value += LF_SCALE;
        /* Fall through */
    case LF_INVERSE2:
        return light->light / ((value * value) / (LF_SCALE * LF_SCALE));
    case LF_LINEAR:
        if (light->light > 0)
            return (light->light - value > 0) ? light->light - value : 0;
        else
            return (light->light + value < 0) ? light->light + value : 0;
    default:
        Error("Internal error: unknown light formula");
    }
}

static inline void
Light_Add(lightsample_t *sample, const vec_t light, const vec3_t color, const vec3_t direction)
{
    sample->light += light;
    VectorMA(sample->color, light / 255.0f, color, sample->color);
    VectorMA(sample->direction, light, direction, sample->direction);
}

static inline void
Light_ClampMin(lightsample_t *sample, const vec_t light, const vec3_t color)
{
    int i;

    if (sample->light < light) {
        sample->light = light;
        for (i = 0; i < 3; i++)
            if (sample->color[i] < color[i] * light / 255.0f)
                sample->color[i] = color[i] * light / 255.0f;
    }
}

/*
 * ============
 * Dirt_GetScaleFactor
 *
 * returns scale factor for dirt/ambient occlusion
 * ============
 */
static inline vec_t
Dirt_GetScaleFactor(vec_t occlusion, const entity_t *entity, const modelinfo_t *modelinfo)
{
    vec_t light_dirtgain = dirtGain;
    vec_t light_dirtscale = dirtScale;
    vec_t outDirt;
    qboolean usedirt;

    /* is dirt processing disabled entirely? */
    if (!dirty)
        return 1.0f;
    if (modelinfo != NULL && modelinfo->nodirt)
        return 1.0f;

    /* should this light be affected by dirt? */
    if (entity) {
        if (entity->dirt == -1) {
            usedirt = false;
        } else if (entity->dirt == 1) {
            usedirt = true;
        } else {
            usedirt = globalDirt;
        }
    } else {
        /* no entity is provided, assume the caller wants dirt */
        usedirt = true;
    }

    /* if not, quit */
    if (!usedirt)
        return 1.0;

    /* override the global scale and gain values with the light-specific
       values, if present */
    if (entity) {
        if (entity->dirtgain)
            light_dirtgain = entity->dirtgain;
        if (entity->dirtscale)
            light_dirtscale = entity->dirtscale;
    }

    /* early out */
    if ( occlusion <= 0.0f ) {
        return 1.0f;
    }

    /* apply gain (does this even do much? heh) */
    outDirt = pow( occlusion, light_dirtgain );
    if ( outDirt > 1.0f ) {
        outDirt = 1.0f;
    }

    /* apply scale */
    outDirt *= light_dirtscale;
    if ( outDirt > 1.0f ) {
        outDirt = 1.0f;
    }

    /* return to sender */
    return 1.0f - outDirt;
}

/*
 * ================
 * CullLight
 * 
 * Returns true if the given light doesn't reach lightsurf.
 * ================
 */
static inline qboolean
CullLight(const entity_t *entity, const lightsurf_t *lightsurf)
{
    vec3_t distvec;
    vec_t dist;
    
    VectorSubtract(entity->origin, lightsurf->origin, distvec);
    dist = VectorLength(distvec) - lightsurf->radius;
    
    /* light is inside surface bounding sphere => can't cull */
    if (dist < 0)
        return false;
    
    /* return true if the light level at the closest point on the
     surface bounding sphere to the light source is <= fadegate.
     need fabs to handle antilights. */
    return fabs(GetLightValue(&entity->light, entity, dist)) <= fadegate;
}

static byte thepalette[768] =
{
0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};
static void Matrix4x4_CM_Transform4(const float *matrix, const float *vector, float *product)
{
    product[0] = matrix[0]*vector[0] + matrix[4]*vector[1] + matrix[8]*vector[2] + matrix[12]*vector[3];
    product[1] = matrix[1]*vector[0] + matrix[5]*vector[1] + matrix[9]*vector[2] + matrix[13]*vector[3];
    product[2] = matrix[2]*vector[0] + matrix[6]*vector[1] + matrix[10]*vector[2] + matrix[14]*vector[3];
    product[3] = matrix[3]*vector[0] + matrix[7]*vector[1] + matrix[11]*vector[2] + matrix[15]*vector[3];
}
static qboolean Matrix4x4_CM_Project (const vec3_t in, vec3_t out, const float *modelviewproj)
{
    qboolean result = true;

    float v[4], tempv[4];
    tempv[0] = in[0];
    tempv[1] = in[1];
    tempv[2] = in[2];
    tempv[3] = 1;

    Matrix4x4_CM_Transform4(modelviewproj, tempv, v);

    v[0] /= v[3];
    v[1] /= v[3];
    if (v[2] < 0)
        result = false; //too close to the view
    v[2] /= v[3];

    out[0] = (1+v[0])/2;
    out[1] = (1+v[1])/2;
    out[2] = (1+v[2])/2;
    if (out[2] > 1)
        result = false; //beyond far clip plane
    return result;
}
static void LightFace_SampleMipTex(miptex_t *tex, const float *projectionmatrix, const vec3_t point, float *result)
{
    //okay, yes, this is weird, yes we're using a vec3_t for a coord...
    //this is because we're treating it like a cubemap. why? no idea.
    float sfrac, tfrac, weight[4];
    int sbase, tbase;
    byte *data = (byte*)tex + tex->offsets[0], *pi[4];

    vec3_t coord;
    if (!Matrix4x4_CM_Project(point, coord, projectionmatrix) || coord[0] <= 0 || coord[0] >= 1 || coord[1] <= 0 || coord[1] >= 1)
        VectorSet(result, 0, 0, 0);
    else
    {
        sfrac = (coord[0]) * tex->width;
        sbase = sfrac;
        sfrac -= sbase;
        tfrac = (1-coord[1]) * tex->height;
        tbase = tfrac;
        tfrac -= tbase;

        pi[0] = thepalette + 3*data[((sbase+0)%tex->width) + (tex->width*((tbase+0)%tex->height))];     weight[0] = (1-sfrac)*(1-tfrac);
        pi[1] = thepalette + 3*data[((sbase+1)%tex->width) + (tex->width*((tbase+0)%tex->height))];     weight[1] = (sfrac)*(1-tfrac);
        pi[2] = thepalette + 3*data[((sbase+0)%tex->width) + (tex->width*((tbase+1)%tex->height))];     weight[2] = (1-sfrac)*(tfrac);
        pi[3] = thepalette + 3*data[((sbase+1)%tex->width) + (tex->width*((tbase+1)%tex->height))];     weight[3] = (sfrac)*(tfrac);
        VectorSet(result, 0, 0, 0);
        result[0]  = weight[0] * pi[0][0];
        result[1]  = weight[0] * pi[0][1];
        result[2]  = weight[0] * pi[0][2];
        result[0] += weight[1] * pi[1][0];
        result[1] += weight[1] * pi[1][1];
        result[2] += weight[1] * pi[1][2];
        result[0] += weight[2] * pi[2][0];
        result[1] += weight[2] * pi[2][1];
        result[2] += weight[2] * pi[2][2];
        result[0] += weight[3] * pi[3][0];
        result[1] += weight[3] * pi[3][1];
        result[2] += weight[3] * pi[3][2];
        VectorScale(result, 2, result);
    }
}

static void
ProjectPointOntoPlane(const vec3_t point, const plane_t *plane, vec3_t out)
{
    vec_t dist = DotProduct(point, plane->normal) - plane->dist;
    VectorMA(point, -dist, plane->normal, out);
}

/*
 * ================
 * LightFace_Entity
 * ================
 */
static void
LightFace_Entity(const entity_t *entity, const lightsample_t *light,
                 const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const dmodel_t *shadowself;
    const vec_t *surfpoint, *surfnorm;
    int i;
    qboolean hit;
    vec_t dist, add, angle, spotscale;
    lightsample_t *sample;
    lightmap_t *lightmap;
    qboolean curved = lightsurf->curved;

    dist = DotProduct(entity->origin, plane->normal) - plane->dist;

    /* don't bother with lights behind the surface */
    if (dist < 0 && !curved)
        return;

    /* sphere cull surface and light */
    if (CullLight(entity, lightsurf))
        return;

    /*
     * Check it for real
     */
    hit = false;
    lightmap = Lightmap_ForStyle(lightmaps, entity->style, lightsurf);
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    sample = lightmap->samples;
    surfpoint = lightsurf->points[0];
    surfnorm = lightsurf->normals[0];
    for (i = 0; i < lightsurf->numpoints; i++, sample++, surfpoint += 3, surfnorm += 3) {
        vec3_t surfpoint_actual, ray;

        // surfpoint is hovering above the face by 1 unit (see CalcPoints).
        // We the point exactly on the plane for calculating the light angle,
        // otherwise lights within 1 unit of a surface don't work
        ProjectPointOntoPlane(surfpoint, &lightsurf->plane, surfpoint_actual);
        
        VectorSubtract(entity->origin, surfpoint_actual, ray);
        dist = VectorLength(ray);

        /* Quick distance check first */
        if (fabs(GetLightValue(&entity->light, entity, dist)) <= fadegate)
            continue;

        /* Check spotlight cone */
        VectorScale(ray, 1.0 / dist, ray);
        angle = DotProduct(ray, surfnorm);
        if (angle <= 0)
                continue;       //curved surfaces are allowed to have the light 'behind' the surface. omitting this check results in erroneous darkening on these surfaces
        spotscale = 1;
        if (entity->spotlight) {
            vec_t falloff = DotProduct(entity->spotvec, ray);
            if (falloff > entity->spotfalloff)
                continue;
            if (falloff > entity->spotfalloff2) {
                /* Interpolate between the two spotlight falloffs */
                spotscale = falloff - entity->spotfalloff2;
                spotscale /= entity->spotfalloff - entity->spotfalloff2;
                spotscale = 1.0 - spotscale;
            }
        }

        if (!TestLight(entity->origin, surfpoint, shadowself))
            continue;

        if (dist >= 0)  //anglescale normally lights the surface more than it otherwise should, meaning the dark side would be visible if there were no occlusion. naturally, this doesn't work when we're relaxing occlusion to faciliate curves.
        angle = (1.0 - entity->anglescale) + entity->anglescale * angle;
        add = GetLightValue(light, entity, dist) * angle * spotscale;
        add *= Dirt_GetScaleFactor(lightsurf->occlusion[i], entity, modelinfo);

        if (entity->projectedmip)
        {
            vec3_t col;
            VectorCopy(light->color, col);
            VectorScale(ray, 255, col);
            LightFace_SampleMipTex(entity->projectedmip, entity->projectionmatrix, surfpoint_actual, col);
            Light_Add(sample, add, col, ray);           
        }
        else
        Light_Add(sample, add, light->color, ray);

        /* Check if we really hit, ignore tiny lights */
        /* ericw -- never ignore generated lights, which can be tiny and need
           the additive effect of lots hitting */
        if (!hit && (sample->light >= 1 || entity->generated))
            hit = true;
    }

    if (hit)
        Lightmap_Save(lightmaps, lightsurf, lightmap, entity->style);
}

/*
 * =============
 * LightFace_Sky
 * =============
 */
static void
LightFace_Sky(const sun_t *sun, const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const dmodel_t *shadowself;
    const vec_t *surfpoint, *surfnorm;
    int i;
    qboolean hit;
    vec3_t incoming;
    vec_t angle;
    lightsample_t *sample;
    lightmap_t *lightmap;
    qboolean curved = lightsurf->curved;

    /* Don't bother if surface facing away from sun */
    if (DotProduct(sun->sunvec, plane->normal) < -ANGLE_EPSILON && !curved)
        return;

    /* if sunlight is set, use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    VectorCopy(sun->sunvec, incoming);
    VectorNormalize(incoming);
    
    /* Check each point... */
    hit = false;
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    sample = lightmap->samples;
    surfpoint = lightsurf->points[0];
    surfnorm = lightsurf->normals[0];
    for (i = 0; i < lightsurf->numpoints; i++, sample++, surfpoint += 3, surfnorm += 3) {
        vec_t value;
        if (!TestSky(surfpoint, sun->sunvec, shadowself))
            continue;

        angle = DotProduct(incoming, surfnorm);
        if (angle < 0) angle = 0;

        angle = (1.0 - sun->anglescale) + sun->anglescale * angle;
        value = angle * sun->sunlight.light;
        if (sun->dirt)
            value *= Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
        Light_Add(sample, value, sun->sunlight.color, sun->sunvec);
        if (!hit/* && (sample->light >= 1)*/)
            hit = true;
    }

    if (hit)
        Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/*
 * ============
 * LightFace_Min
 * ============
 */
static void
LightFace_Min(const lightsample_t *light,
              const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const dmodel_t *shadowself;
    entity_t **entity;
    const vec_t *surfpoint;
    qboolean hit, trace;
    int i, j;
    lightsample_t *sample;
    lightmap_t *lightmap;

    /* Find a style 0 lightmap */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    hit = false;
    sample = lightmap->samples;
    for (i = 0; i < lightsurf->numpoints; i++, sample++) {
        vec_t value = light->light;
        if (minlightDirt)
            value *= Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
        if (addminlight)
            Light_Add(sample, value, light->color, vec3_origin);
        else
            Light_ClampMin(sample, value, light->color);
        if (!hit && sample->light >= 1)
            hit = true;
    }

    if (hit)
        Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
    
    /* Cast rays for local minlight entities */
    shadowself = modelinfo->shadowself ? modelinfo->model : NULL;
    for (entity = lights; *entity; entity++) {
        if ((*entity)->formula != LF_LOCALMIN)
            continue;

        lightmap = Lightmap_ForStyle(lightmaps, (*entity)->style, lightsurf);

        hit = false;
        sample = lightmap->samples;
        surfpoint = lightsurf->points[0];
        for (j = 0; j < lightsurf->numpoints; j++, sample++, surfpoint += 3) {
            if (addminlight || sample->light < (*entity)->light.light) {
                vec_t value = (*entity)->light.light;
                trace = TestLight((*entity)->origin, surfpoint, shadowself);
                if (!trace)
                    continue;
                value *= Dirt_GetScaleFactor(lightsurf->occlusion[j], (*entity), modelinfo);
                if (addminlight)
                    Light_Add(sample, value, (*entity)->light.color, vec3_origin);
                else
                    Light_ClampMin(sample, value, (*entity)->light.color);
            }
            if (!hit && sample->light >= 1)
                hit = true;
        }
        
        if (hit)
            Lightmap_Save(lightmaps, lightsurf, lightmap, (*entity)->style);
    }
}

/*
 * =============
 * LightFace_DirtDebug
 * =============
 */
static void
LightFace_DirtDebug(const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    int i;
    lightsample_t *sample;
    lightmap_t *lightmap;

    /* use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);

    /* Overwrite each point with the dirt value for that sample... */
    sample = lightmap->samples;
    for (i = 0; i < lightsurf->numpoints; i++, sample++) {
        sample->light = 255 * Dirt_GetScaleFactor(lightsurf->occlusion[i], NULL, modelinfo);
        VectorSet(sample->color, sample->light, sample->light, sample->light);
    }

    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/*
 * =============
 * LightFace_PhongDebug
 * =============
 */
static void
LightFace_PhongDebug(const lightsurf_t *lightsurf, lightmap_t *lightmaps)
{
    int i;
    lightsample_t *sample;
    lightmap_t *lightmap;
    
    /* use a style 0 light map */
    lightmap = Lightmap_ForStyle(lightmaps, 0, lightsurf);
    
    /* Overwrite each point with the normal for that sample... */
    sample = lightmap->samples;
    for (i = 0; i < lightsurf->numpoints; i++, sample++) {
        const vec3_t vec3_one = { 1.0f, 1.0f, 1.0f };
        vec3_t normal_as_color;
        // scale from [-1..1] to [0..1], then multiply by 255
        VectorCopy(lightsurf->normals[i], normal_as_color);
        VectorAdd(normal_as_color, vec3_one, normal_as_color);
        VectorScale(normal_as_color, 0.5, normal_as_color);
        VectorScale(normal_as_color, 255, normal_as_color);
        
        sample->light = 255;
        VectorCopy(normal_as_color, sample->color);
    }
    
    Lightmap_Save(lightmaps, lightsurf, lightmap, 0);
}

/* Dirtmapping borrowed from q3map2, originally by RaP7oR */

#define DIRT_NUM_ANGLE_STEPS        16
#define DIRT_NUM_ELEVATION_STEPS    3
#define DIRT_NUM_VECTORS            ( DIRT_NUM_ANGLE_STEPS * DIRT_NUM_ELEVATION_STEPS )

static vec3_t dirtVectors[ DIRT_NUM_VECTORS ];
static int numDirtVectors = 0;

/*
 * ============
 * SetupDirt
 *
 * sets up dirtmap (ambient occlusion)
 * ============
 */
void SetupDirt( void ) {
    int i, j;
    float angle, elevation, angleStep, elevationStep;

    /* note it */
    logprint("--- SetupDirt ---\n" );

    /* clamp dirtAngle */
    if ( dirtAngle <= 1.0f ) {
        dirtAngle = 1.0f;
    }
    if ( dirtAngle >= 90.0f) {
        dirtAngle = 90.0f;
    }
    
    /* calculate angular steps */
    angleStep = DEG2RAD( 360.0f / DIRT_NUM_ANGLE_STEPS );
    elevationStep = DEG2RAD( dirtAngle / DIRT_NUM_ELEVATION_STEPS );

    /* iterate angle */
    angle = 0.0f;
    for ( i = 0, angle = 0.0f; i < DIRT_NUM_ANGLE_STEPS; i++, angle += angleStep ) {
        /* iterate elevation */
        for ( j = 0, elevation = elevationStep * 0.5f; j < DIRT_NUM_ELEVATION_STEPS; j++, elevation += elevationStep ) {
            dirtVectors[ numDirtVectors ][ 0 ] = sin( elevation ) * cos( angle );
            dirtVectors[ numDirtVectors ][ 1 ] = sin( elevation ) * sin( angle );
            dirtVectors[ numDirtVectors ][ 2 ] = cos( elevation );
            numDirtVectors++;
        }
    }

    /* emit some statistics */
    logprint("%9d dirtmap vectors\n", numDirtVectors );
}

/*
 * ============
 * DirtTrace
 *
 * returns true if the trace from start to stop hits something solid,
 * or if it started in the void.
 * ============
 */
qboolean
DirtTrace(const vec3_t start, const vec3_t stop, const dmodel_t *self, vec3_t hitpoint_out)
{
    const dmodel_t *const *model;
    const int traceflags = TRACE_HIT_SOLID | TRACE_HIT_SKY;
    int result = TRACE_HIT_NONE;
    tracepoint_t hitpoint;

    if (self) {
        result = TraceLine(self, traceflags, start, stop, &hitpoint);
        if (result == -TRACE_HIT_SOLID) {
            /* We started in the void, which ideally wouldn't happen, 
               but does (say on e1m1). Return the start point as the hitpoint,
               which will make fully black dirt.
             */
            VectorCopy(start, hitpoint_out);
            return true;
        } else if (result == TRACE_HIT_SOLID) {
            VectorCopy(hitpoint.point, hitpoint_out);
            return true;
        }
    }

    /* Check against the list of global shadow casters */
    for (model = tracelist; *model; model++) {
        if (*model == self)
            continue;
        result = TraceLine(*model, traceflags, start, stop, &hitpoint);
        if (result == -TRACE_HIT_SOLID) {
            VectorCopy(start, hitpoint_out);
            return true;
        } else if (result == TRACE_HIT_SOLID) {
            VectorCopy(hitpoint.point, hitpoint_out);
            return true;
        }
    }

    return false;
}

/*
 * ============
 * DirtForSample
 * ============
 */
static vec_t
DirtForSample(const dmodel_t *model, const vec3_t origin, const vec3_t normal){
    int i;
    float gatherDirt, angle, elevation, ooDepth;
    vec3_t worldUp, myUp, myRt, temp, direction, displacement;
    vec3_t traceEnd, traceHitpoint;

    /* dummy check */
    if ( !dirty ) {
        return 1.0f;
    }
    
    /* setup */
    gatherDirt = 0.0f;
    ooDepth = 1.0f / dirtDepth;

    /* check if the normal is aligned to the world-up */
    if ( normal[ 0 ] == 0.0f && normal[ 1 ] == 0.0f ) {
        if ( normal[ 2 ] == 1.0f ) {
            VectorSet( myRt, 1.0f, 0.0f, 0.0f );
            VectorSet( myUp, 0.0f, 1.0f, 0.0f );
        } else if ( normal[ 2 ] == -1.0f ) {
            VectorSet( myRt, -1.0f, 0.0f, 0.0f );
            VectorSet( myUp,  0.0f, 1.0f, 0.0f );
        }
    } else {
        VectorSet( worldUp, 0.0f, 0.0f, 1.0f );
        CrossProduct( normal, worldUp, myRt );
        VectorNormalize( myRt );
        CrossProduct( myRt, normal, myUp );
        VectorNormalize( myUp );
    }

    /* 1 = random mode, 0 (well everything else) = non-random mode */
    if ( dirtMode == 1 ) {
        /* iterate */
        for ( i = 0; i < numDirtVectors; i++ ) {
            /* get random vector */
            angle = Random() * DEG2RAD( 360.0f );
            elevation = Random() * DEG2RAD( dirtAngle );
            temp[ 0 ] = cos( angle ) * sin( elevation );
            temp[ 1 ] = sin( angle ) * sin( elevation );
            temp[ 2 ] = cos( elevation );

            /* transform into tangent space */
            direction[ 0 ] = myRt[ 0 ] * temp[ 0 ] + myUp[ 0 ] * temp[ 1 ] + normal[ 0 ] * temp[ 2 ];
            direction[ 1 ] = myRt[ 1 ] * temp[ 0 ] + myUp[ 1 ] * temp[ 1 ] + normal[ 1 ] * temp[ 2 ];
            direction[ 2 ] = myRt[ 2 ] * temp[ 0 ] + myUp[ 2 ] * temp[ 1 ] + normal[ 2 ] * temp[ 2 ];

            /* set endpoint */
            VectorMA( origin, dirtDepth, direction, traceEnd );

            /* trace */
            if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
                VectorSubtract( traceHitpoint, origin, displacement );
                gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
            }
        }
    } else {
        /* iterate through ordered vectors */
        for ( i = 0; i < numDirtVectors; i++ ) {
            /* transform vector into tangent space */
            direction[ 0 ] = myRt[ 0 ] * dirtVectors[ i ][ 0 ] + myUp[ 0 ] * dirtVectors[ i ][ 1 ] + normal[ 0 ] * dirtVectors[ i ][ 2 ];
            direction[ 1 ] = myRt[ 1 ] * dirtVectors[ i ][ 0 ] + myUp[ 1 ] * dirtVectors[ i ][ 1 ] + normal[ 1 ] * dirtVectors[ i ][ 2 ];
            direction[ 2 ] = myRt[ 2 ] * dirtVectors[ i ][ 0 ] + myUp[ 2 ] * dirtVectors[ i ][ 1 ] + normal[ 2 ] * dirtVectors[ i ][ 2 ];

            /* set endpoint */
            VectorMA( origin, dirtDepth, direction, traceEnd );
            
            /* trace */
            if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
                VectorSubtract( traceHitpoint, origin, displacement );
                gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
            }
        }
    }

    /* direct ray */
    VectorMA( origin, dirtDepth, normal, traceEnd );
    
    /* trace */
    if (DirtTrace(origin, traceEnd, model, traceHitpoint)) {
        VectorSubtract( traceHitpoint, origin, displacement );
        gatherDirt += 1.0f - ooDepth * VectorLength( displacement );
    }

    /* save gatherDirt, the rest of the scaling of the dirt value is done
       per-light */

    return gatherDirt / ( numDirtVectors + 1 );
}


/*
 * ============
 * LightFace_CalculateDirt
 * ============
 */
static void
LightFace_CalculateDirt(lightsurf_t *lightsurf)
{
    const modelinfo_t *modelinfo = lightsurf->modelinfo;
    const plane_t *plane = &lightsurf->plane;
    const vec_t *surfpoint;
    int i;

    /* Check each point... */
    surfpoint = lightsurf->points[0];
    for (i = 0; i < lightsurf->numpoints; i++, surfpoint += 3) {
        lightsurf->occlusion[i] = DirtForSample(modelinfo->model, surfpoint, plane->normal);
    }
}


static void
WriteLightmaps(bsp2_dface_t *face, facesup_t *facesup, const lightsurf_t *lightsurf,
               const lightmap_t *lightmaps)
{
    int numstyles, size, mapnum, width, s, t, i, j;
    vec_t light, maxcolor;
    vec3_t color, direction;
    byte *out, *lit, *lux;

    /* count the styles */
    numstyles = 0;
    for (mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++)
    {
        if (lightmaps[mapnum].style == 255)
            break;
            numstyles++;
    }

    /* update face info (either core data or supplementary stuff) */
    if (facesup)
    {
        facesup->extent[0] = lightsurf->texsize[0] + 1;
        facesup->extent[1] = lightsurf->texsize[1] + 1;
        for (mapnum = 0; mapnum < numstyles; mapnum++)
            facesup->styles[mapnum] = lightmaps[mapnum].style;
        for (; mapnum < MAXLIGHTMAPS; mapnum++)
            facesup->styles[mapnum] = 255;
        facesup->lmscale = lightsurf->lightmapscale;
    }
    else
    {
        for (mapnum = 0; mapnum < numstyles; mapnum++)
            face->styles[mapnum] = lightmaps[mapnum].style;
        for (; mapnum < MAXLIGHTMAPS; mapnum++)
            face->styles[mapnum] = 255;
    }

    if (!numstyles)
        return;

    size = (lightsurf->texsize[0] + 1) * (lightsurf->texsize[1] + 1);
    GetFileSpace(&out, &lit, &lux, size * numstyles);
    if (facesup)
        facesup->lightofs = out - filebase;
    else
        face->lightofs = out - filebase;

    width = (lightsurf->texsize[0] + 1) * oversample;
    for (mapnum = 0; mapnum < MAXLIGHTMAPS; mapnum++) {
        if (lightmaps[mapnum].style == 255)
            break;

        for (t = 0; t <= lightsurf->texsize[1]; t++) {
            for (s = 0; s <= lightsurf->texsize[0]; s++) {

                /* Take the average of any oversampling */
                VectorCopy(vec3_origin, color);
                VectorCopy(vec3_origin, direction);
                for (i = 0; i < oversample; i++) {
                    for (j = 0; j < oversample; j++) {
                        const int col = (s*oversample) + j;
                        const int row = (t*oversample) + i;

                        const lightsample_t *sample = lightmaps[mapnum].samples + (row * width) + col;

                        VectorAdd(color, sample->color, color);
                        VectorAdd(direction, sample->direction, direction);
                    }
                }
                VectorScale(color, 1.0 / oversample / oversample, color);

                /* Scale and clamp any out-of-range samples */
                maxcolor = 0;
                VectorScale(color, rangescale, color);
                for (i = 0; i < 3; i++)
                    color[i] = pow( color[i] / 255.0f, 1.0 / lightmapgamma ) * 255.0f;
                for (i = 0; i < 3; i++)
                    if (color[i] > maxcolor)
                        maxcolor = color[i];
                if (maxcolor > 255)
                    VectorScale(color, 255.0f / maxcolor, color);

                *lit++ = color[0];
                *lit++ = color[1];
                *lit++ = color[2];

                /* Average the color to get the value to write to the
                   .bsp lightmap. this avoids issues with some engines
                   that require the lit and internal lightmap to have the same
                   intensity. (MarkV, some QW engines)
                 */
                light = (color[0] + color[1] + color[2]) / 3.0;
                if (light < 0) light = 0;
                if (light > 255) light = 255;
                *out++ = light;

                if (lux)
                {
                        vec3_t temp;
                        int v;
                        temp[0] = DotProduct(direction, lightsurf->snormal);
                        temp[1] = DotProduct(direction, lightsurf->tnormal);
                        temp[2] = DotProduct(direction, lightsurf->plane.normal);
                
                        if (!temp[0] && !temp[1] && !temp[2])
                                VectorSet(temp, 0, 0, 1);
                        else
                                VectorNormalize(temp);

                        v = (temp[0]+1)*128;  *lux++ = (v>255)?255:v;
                        v = (temp[1]+1)*128;  *lux++ = (v>255)?255:v;
                        v = (temp[2]+1)*128;  *lux++ = (v>255)?255:v;
                }
            }
        }
    }
}

struct ltface_ctx *LightFaceInit(const bsp2_t *bsp)
{
    //windows stack probes can get expensive when its 64mb...
    //also, this avoids stack overflows, or the need to guess stack sizes.
    struct ltface_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx)
        ctx->bsp = bsp;
    return ctx;
}
void LightFaceShutdown(struct ltface_ctx *ctx)
{
    int i;
    for (i = 0; i < MAXLIGHTMAPS + 1; i++)
    {
        if (ctx->lightmaps[i].samples)
            free(ctx->lightmaps[i].samples);
    }
    
    if (ctx->lightsurf.points)
        free(ctx->lightsurf.points);
    
    if (ctx->lightsurf.normals)
        free(ctx->lightsurf.normals);
    
    if (ctx->lightsurf.occlusion)
        free(ctx->lightsurf.occlusion);
    
    free(ctx);
}

/*
 * ============
 * LightFace
 * ============
 */
void
LightFace(bsp2_dface_t *face, facesup_t *facesup, const modelinfo_t *modelinfo, struct ltface_ctx *ctx)
{
    int i, j, k;
    const entity_t *entity;
    entity_t **lighte;
    lightsample_t *sample;
    sun_t *sun;

    const bsp2_t *bsp = ctx->bsp;
    lightmap_t *lightmaps = ctx->lightmaps;
    lightsurf_t *lightsurf = &ctx->lightsurf;

    /* One extra lightmap is allocated to simplify handling overflow */

    /* some surfaces don't need lightmaps */
    if (facesup)
    {
        facesup->lightofs = -1;
        for (i = 0; i < MAXLIGHTMAPS; i++)
            facesup->styles[i] = 255;
    }
    else
    {
    face->lightofs = -1;
    for (i = 0; i < MAXLIGHTMAPS; i++)
        face->styles[i] = 255;
    }
    if (bsp->texinfo[face->texinfo].flags & TEX_SPECIAL)
    {
        int texnum = bsp->texinfo[face->texinfo].miptex;
        dmiptexlump_t *miplump = bsp->dtexdata.header;
        miptex_t *miptex;
        int turbtype;
        if (!lightturb)
        return;
        if (!miplump->dataofs[texnum])
                return; //sometimes the texture just wasn't written. including its name.
        miptex = (miptex_t*)(bsp->dtexdata.base + miplump->dataofs[texnum]);
        if (*miptex->name != '*')
            return;     //non-water surfaces should still not be lit
        //texture name starts with a *, so light it anyway.
        if (!Q_strncasecmp(miptex->name, "*tele", 5))
                turbtype = 3;
        else if (!Q_strncasecmp(miptex->name, "*lava", 5))
                turbtype = 2;
        else if (!Q_strncasecmp(miptex->name, "*slime", 6))
                turbtype = 1;
        else
                turbtype = 0;
        if (!(lightturb & (1u<<turbtype)))
                return;
    }

    Lightsurf_Init(modelinfo, face, bsp, lightsurf, facesup);
    Lightmaps_Init(lightsurf, lightmaps, MAXLIGHTMAPS + 1);

    /* calculate dirt (ambient occlusion) but don't use it yet */
    if (dirty)
        LightFace_CalculateDirt(lightsurf);

    /*
     * The lighting procedure is: cast all positive lights, fix
     * minlight levels, then cast all negative lights. Finally, we
     * clamp any values that may have gone negative.
     */

    /* positive lights */
    for (lighte = lights; (entity = *lighte); lighte++)
    {
        if (entity->formula == LF_LOCALMIN)
            continue;
        if (entity->light.light > 0)
            LightFace_Entity(entity, &entity->light, lightsurf, lightmaps);
    }
    for ( sun = suns; sun; sun = sun->next )
        if (sun->sunlight.light > 0)
            LightFace_Sky (sun, lightsurf, lightmaps);

    /* minlight - Use the greater of global or model minlight. */
    if (modelinfo->minlight.light > minlight.light)
        LightFace_Min(&modelinfo->minlight, lightsurf, lightmaps);
    else
        LightFace_Min(&minlight, lightsurf, lightmaps);

    /* negative lights */
    for (lighte = lights; (entity = *lighte); lighte++)
    {
        if (entity->formula == LF_LOCALMIN)
            continue;
        if (entity->light.light < 0)
            LightFace_Entity(entity, &entity->light, lightsurf, lightmaps);
    }
    for ( sun = suns; sun; sun = sun->next )
        if (sun->sunlight.light < 0)
            LightFace_Sky (sun, lightsurf, lightmaps);

    /* replace lightmaps with AO for debugging */
    if (dirtDebug)
        LightFace_DirtDebug(lightsurf, lightmaps);

    if (phongDebug)
        LightFace_PhongDebug(lightsurf, lightmaps);
    
    /* Fix any negative values */
    for (i = 0; i < MAXLIGHTMAPS; i++) {
        if (lightmaps[i].style == 255)
            break;
        sample = lightmaps[i].samples;
        for (j = 0; j < lightsurf->numpoints; j++, sample++) {
            if (sample->light < 0)
                sample->light = 0;
            for (k = 0; k < 3; k++) {
                if (sample->color[k] < 0) {
                    sample->color[k] = 0;
                }
            }
        }
    }

    /* Perform post-processing if requested */
    if (softsamples > 0) {
        for (i = 0; i < MAXLIGHTMAPS; i++) {
            if (lightmaps[i].style == 255)
                break;
            Lightmap_Soften(&lightmaps[i], lightsurf);
        }
    }

    WriteLightmaps(face, facesup, lightsurf, lightmaps);
}
