#include "gtest/gtest.h"

#include <light/light.hh>

#include <random>
#include <algorithm> // for std::sort

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/epsilon.hpp>

#include <common/mesh.hh>
#include <common/aabb.hh>
#include <common/octree.hh>

using namespace glm;
using namespace std;

static glm::vec4 extendTo4(const glm::vec3 &v) {
    return glm::vec4(v[0], v[1], v[2], 1.0);
}

TEST(mathlib, MakeCDF) {
    
    std::vector<float> pdfUnnormzlied { 25, 50, 25 };
    std::vector<float> cdf = MakeCDF(pdfUnnormzlied);
    
    ASSERT_EQ(3u, cdf.size());
    ASSERT_FLOAT_EQ(0.25, cdf.at(0));
    ASSERT_FLOAT_EQ(0.75, cdf.at(1));
    ASSERT_FLOAT_EQ(1.0, cdf.at(2));
    
    // TODO: return pdf
    ASSERT_EQ(0, SampleCDF(cdf, 0));
    ASSERT_EQ(0, SampleCDF(cdf, 0.1));
    ASSERT_EQ(0, SampleCDF(cdf, 0.25));
    ASSERT_EQ(1, SampleCDF(cdf, 0.26));
    ASSERT_EQ(1, SampleCDF(cdf, 0.75));
    ASSERT_EQ(2, SampleCDF(cdf, 0.76));
    ASSERT_EQ(2, SampleCDF(cdf, 1));
}

static void checkBox(const vector<vec4> &edges, const vector<vec3> &poly) {
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(64,0,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(32,32,0)));
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(32,32,32))); // off plane
    
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-0.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(64.1,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,-0.1,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,64.1,0)));

}

TEST(mathlib, EdgePlanesOfNonConvexPoly) {
    // hourglass, non-convex
    const vector<vec3> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 0,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
//    EXPECT_EQ(vector<vec4>(), edges);
}

TEST(mathlib, SlightlyConcavePoly) {
    const vector<vec3> poly {
        {225.846161, -1744, 1774},
        {248, -1744, 1798},
        {248, -1763.82605, 1799.65222},
        {248, -1764, 1799.66663},
        {248, -1892, 1810.33337},
        {248, -1893.21741, 1810.43481},
        {248, -1921.59998, 1812.80005},
        {248, -1924, 1813},
        {80, -1924, 1631},
        {80, -1744, 1616}
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    ASSERT_FALSE(edges.empty());
    EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, vec3(152.636963, -1814, 1702)));
}

TEST(mathlib, PointInPolygon) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(mathlib, PointInPolygon_DegenerateEdgeHandling) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 0,64,0 }, // repeat of last point
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    checkBox(edges, poly);
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling1) {
    const vector<vec3> poly {
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling2) {
    const vector<vec3> poly {
        {0,0,0},
        {0,0,0},
        {0,0,0},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_DegenerateFaceHandling3) {
    const vector<vec3> poly {
        {0,0,0},
        {10,10,10},
        {20,20,20},
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(0,0,0)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(10,10,10)));
    EXPECT_FALSE(GLM_EdgePlanes_PointInside(edges, vec3(-10,-10,-10)));
}

TEST(mathlib, PointInPolygon_ColinearPointHandling) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edges = GLM_MakeInwardFacingEdgePlanes(poly);
    
    checkBox(edges, poly);
}

TEST(mathlib, ClosestPointOnPolyBoundary) {
    // clockwise
    const vector<vec3> poly {
        { 0,0,0 },   // edge 0 start, edge 3 end
        { 0,64,0 },  // edge 1 start, edge 0 end
        { 64,64,0 }, // edge 2 start, edge 1 end
        { 64,0,0 }   // edge 3 start, edge 2 end
    };
    
    EXPECT_EQ(make_pair(0, vec3(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(0,0,0)));
    
    // Either edge 1 or 2 contain the point vec3(64,64,0), but we expect the first edge to be returned
    EXPECT_EQ(make_pair(1, vec3(64,64,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(100,100,100)));
    EXPECT_EQ(make_pair(2, vec3(64,32,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(100,32,0)));
    
    EXPECT_EQ(make_pair(0, vec3(0,0,0)), GLM_ClosestPointOnPolyBoundary(poly, vec3(-1,-1,0)));
}

TEST(mathlib, PolygonCentroid) {
    // poor test.. but at least checks that the colinear point is treated correctly
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    EXPECT_EQ(vec3(32,32,0), GLM_PolyCentroid(poly));
}

TEST(mathlib, PolygonArea) {
    // poor test.. but at least checks that the colinear point is treated correctly
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    EXPECT_EQ(64.0f * 64.0f, GLM_PolyArea(poly));
}

TEST(mathlib, BarycentricFromPoint) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(vec3(1,0,0), Barycentric_FromPoint(get<0>(tri), tri));
    EXPECT_EQ(vec3(0,1,0), Barycentric_FromPoint(get<1>(tri), tri));
    EXPECT_EQ(vec3(0,0,1), Barycentric_FromPoint(get<2>(tri), tri));
    
    EXPECT_EQ(vec3(0.5, 0.5, 0.0), Barycentric_FromPoint(vec3(0,32,0), tri));
    EXPECT_EQ(vec3(0.0, 0.5, 0.5), Barycentric_FromPoint(vec3(32,32,0), tri));
    EXPECT_EQ(vec3(0.5, 0.0, 0.5), Barycentric_FromPoint(vec3(32,0,0), tri));
}

TEST(mathlib, BarycentricToPoint) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    EXPECT_EQ(get<0>(tri), Barycentric_ToPoint(vec3(1,0,0), tri));
    EXPECT_EQ(get<1>(tri), Barycentric_ToPoint(vec3(0,1,0), tri));
    EXPECT_EQ(get<2>(tri), Barycentric_ToPoint(vec3(0,0,1), tri));
    
    EXPECT_EQ(vec3(0,32,0), Barycentric_ToPoint(vec3(0.5, 0.5, 0.0), tri));
    EXPECT_EQ(vec3(32,32,0), Barycentric_ToPoint(vec3(0.0, 0.5, 0.5), tri));
    EXPECT_EQ(vec3(32,0,0), Barycentric_ToPoint(vec3(0.5, 0.0, 0.5), tri));
}

TEST(mathlib, BarycentricRandom) {
    const tri_t tri = make_tuple<vec3,vec3,vec3>( // clockwise
                                                 { 0,0,0 },
                                                 { 0,64,0 },
                                                 { 64,0,0 }
                                                 );
    
    const auto triAsVec = vector<vec3>{get<0>(tri), get<1>(tri), get<2>(tri)};
    const auto edges = GLM_MakeInwardFacingEdgePlanes(triAsVec);
    const auto plane = GLM_PolyPlane(triAsVec);
    
    for (int i=0; i<100; i++) {
        const float r0 = Random();
        const float r1 = Random();
        
        ASSERT_GE(r0, 0);
        ASSERT_GE(r1, 0);
        ASSERT_LE(r0, 1);
        ASSERT_LE(r1, 1);
        
        const auto bary = Barycentric_Random(r0, r1);
        EXPECT_FLOAT_EQ(1.0f, bary.x + bary.y + bary.z);
        
        const vec3 point = Barycentric_ToPoint(bary, tri);
        EXPECT_TRUE(GLM_EdgePlanes_PointInside(edges, point));
        
        EXPECT_FLOAT_EQ(0.0f, GLM_DistAbovePlane(plane, point));
    }
}

TEST(mathlib, DistAbovePlane) {
    vec4 plane(0, 0, 1, 10);
    vec3 point(100, 100, 100);
    EXPECT_FLOAT_EQ(90, GLM_DistAbovePlane(plane, point));
}

TEST(mathlib, ProjectPointOntoPlane) {
    vec4 plane(0, 0, 1, 10);
    vec3 point(100, 100, 100);
    
    vec3 projected = GLM_ProjectPointOntoPlane(plane, point);
    EXPECT_FLOAT_EQ(100, projected.x);
    EXPECT_FLOAT_EQ(100, projected.y);
    EXPECT_FLOAT_EQ(10, projected.z);
}

TEST(mathlib, InterpolateNormals) {
    // This test relies on the way GLM_InterpolateNormal is implemented
    
    // o--o--o
    // | / / |
    // |//   |
    // o-----o
    
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 }, // colinear
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> normals {
        { 1,0,0 },
        { 0,1,0 },
        { 0,0,1 }, // colinear
        { 0,0,0 },
        { -1,0,0 }
    };
    
    // First try all the known points
    for (int i=0; i<poly.size(); i++) {
        const auto res = GLM_InterpolateNormal(poly, normals, poly.at(i));
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(all(epsilonEqual(normals.at(i), res.second, vec3(POINT_EQUAL_EPSILON))));
    }
    
    {
        const vec3 firstTriCentroid = (poly[0] + poly[1] + poly[2]) / 3.0f;
        const auto res = GLM_InterpolateNormal(poly, normals, firstTriCentroid);
        EXPECT_EQ(true, res.first);
        EXPECT_TRUE(all(epsilonEqual(vec3(1/3.0f), res.second, vec3(POINT_EQUAL_EPSILON))));
    }
    
    // Outside poly
    EXPECT_FALSE(GLM_InterpolateNormal(poly, normals, vec3(-0.1, 0, 0)).first);
}

static bool pointsEqualEpsilon(const vec3 &a, const vec3 &b, const float epsilon) {
    return all(epsilonEqual(a, b, vec3(epsilon)));
}

static bool polysEqual(const vector<vec3> &p1, const vector<vec3> &p2) {
    if (p1.size() != p2.size())
        return false;
    for (int i=0; i<p1.size(); i++) {
        if (!pointsEqualEpsilon(p1[i], p2[i], POINT_EQUAL_EPSILON))
            return false;
    }
    return true;
}

TEST(mathlib, ClipPoly1) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> frontRes {
        { 0,0,0 },
        { 0,64,0 },
        { 32,64,0 },
        { 32,0,0 }
    };

    const vector<vec3> backRes {
        { 32,64,0 },
        { 64,64,0 },
        { 64,0,0 },
        { 32,0,0 }
    };
    
    auto clipRes = GLM_ClipPoly(poly, vec4(-1,0,0,-32));
    
    EXPECT_TRUE(polysEqual(frontRes, clipRes.first));
    EXPECT_TRUE(polysEqual(backRes, clipRes.second));
}

TEST(mathlib, ShrinkPoly1) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> shrunkPoly {
        { 1,1,0 },
        { 1,63,0 },
        { 63,63,0 },
        { 63,1,0 }
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

TEST(mathlib, ShrinkPoly2) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const vector<vec3> shrunkPoly {
        { 1.0f + sqrtf(2.0f), 1.0f, 0.0f },
        { 63.0f, 63.0f - sqrtf(2.0f), 0.0f },
        { 63,1,0 },
    };
    
    const auto actualShrunk = GLM_ShrinkPoly(poly, 1.0f);
    
    EXPECT_TRUE(polysEqual(shrunkPoly, actualShrunk));
}

static const float MANGLE_EPSILON = 0.1f;

TEST(light, vec_from_mangle) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(1,0,0), vec_from_mangle(vec3(0,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(-1,0,0), vec_from_mangle(vec3(180,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,1), vec_from_mangle(vec3(0,90,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,-1), vec_from_mangle(vec3(0,-90,0)), MANGLE_EPSILON));
}

TEST(light, mangle_from_vec) {
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,0,0), mangle_from_vec(vec3(1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(180,0,0), mangle_from_vec(vec3(-1,0,0)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,90,0), mangle_from_vec(vec3(0,0,1)), MANGLE_EPSILON));
    EXPECT_TRUE(pointsEqualEpsilon(vec3(0,-90,0), mangle_from_vec(vec3(0,0,-1)), MANGLE_EPSILON));
    
    for (int yaw = -179; yaw <= 179; yaw++) {
        for (int pitch = -89; pitch <= 89; pitch++) {
            const vec3 origMangle = vec3(yaw, pitch, 0);
            const vec3 vec = vec_from_mangle(origMangle);
            const vec3 roundtrip = mangle_from_vec(vec);
            EXPECT_TRUE(pointsEqualEpsilon(origMangle, roundtrip, MANGLE_EPSILON));
        }
    }
}

TEST(mathlib, bilinearInterpolate) {
    const vec4 v1(0,1,2,3);
    const vec4 v2(4,5,6,7);
    const vec4 v3(1,1,1,1);
    const vec4 v4(2,2,2,2);
    
    EXPECT_EQ(v1, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 0.0f));
    EXPECT_EQ(v2, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 0.0f));
    EXPECT_EQ(v3, bilinearInterpolate(v1, v2, v3, v4, 0.0f, 1.0f));
    EXPECT_EQ(v4, bilinearInterpolate(v1, v2, v3, v4, 1.0f, 1.0f));
    
    EXPECT_EQ(vec4(1.5,  1.5,  1.5,  1.5),  bilinearInterpolate(v1, v2, v3, v4, 0.5f, 1.0f));
    EXPECT_EQ(vec4(2,    3,    4,    5),    bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.0f));
    EXPECT_EQ(vec4(1.75, 2.25, 2.75, 3.25), bilinearInterpolate(v1, v2, v3, v4, 0.5f, 0.5f));
}

TEST(mathlib, bilinearWeightsAndCoords) {
    const auto res = bilinearWeightsAndCoords(vec2(0.5, 0.25), ivec2(2,2));
    
    vec2 sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const ivec2 intPos = res[i].first;
        sum += vec2(intPos) * weight;
    }
    EXPECT_EQ(vec2(0.5, 0.25), sum);
}

TEST(mathlib, bilinearWeightsAndCoords2) {
    const auto res = bilinearWeightsAndCoords(vec2(1.5, 0.5), ivec2(2,2));
    
    vec2 sum(0);
    for (int i=0; i<4; i++) {
        const float weight = res[i].second;
        const ivec2 intPos = res[i].first;
        sum += vec2(intPos) * weight;
    }
    EXPECT_EQ(vec2(1.0, 0.5), sum);
}

TEST(mathlib, pointsAlongLine) {
    const auto res = PointsAlongLine(vec3(1,0,0), vec3(3.5, 0, 0), 1.5f);

    ASSERT_EQ(2, res.size());
    ASSERT_TRUE(pointsEqualEpsilon(vec3(1,0,0), res[0], POINT_EQUAL_EPSILON));
    ASSERT_TRUE(pointsEqualEpsilon(vec3(2.5,0,0), res[1], POINT_EQUAL_EPSILON));
}

TEST(mathlib, RandomPointInPoly) {
    const vector<vec3> poly {
        { 0,0,0 },
        { 0,32,0 }, // colinear point
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    
    const auto edgeplanes = GLM_MakeInwardFacingEdgePlanes(poly);
    
    glm::vec3 min(FLT_MAX);
    glm::vec3 max(-FLT_MAX);
    glm::vec3 avg(0);
    
    const int N=100;
    for (int i=0; i<N; i++) {
        const glm::vec3 point = GLM_PolyRandomPoint(poly);
        ASSERT_TRUE(GLM_EdgePlanes_PointInside(edgeplanes, point));
        
        //std::cout << "point: " << glm::to_string(point) << std::endl;
        
        min = glm::min(min, point);
        max = glm::max(max, point);
        avg += point;
    }
    avg /= N;
    
    ASSERT_LT(min.x, 4);
    ASSERT_LT(min.y, 4);
    ASSERT_EQ(min.z, 0);
    
    ASSERT_GT(max.x, 60);
    ASSERT_GT(max.y, 60);
    ASSERT_EQ(max.z, 0);
    
    ASSERT_LT(glm::length(avg - glm::vec3(32, 32, 0)), 4);
}

TEST(mathlib, FractionOfLine) {
    ASSERT_FLOAT_EQ(0, FractionOfLine(vec3(0,0,0), vec3(1,1,1), vec3(0,0,0)));
    ASSERT_FLOAT_EQ(0.5, FractionOfLine(vec3(0,0,0), vec3(1,1,1), vec3(0.5, 0.5, 0.5)));
    ASSERT_FLOAT_EQ(1, FractionOfLine(vec3(0,0,0), vec3(1,1,1), vec3(1,1,1)));
    ASSERT_FLOAT_EQ(2, FractionOfLine(vec3(0,0,0), vec3(1,1,1), vec3(2,2,2)));
    ASSERT_FLOAT_EQ(-1, FractionOfLine(vec3(0,0,0), vec3(1,1,1), vec3(-1,-1,-1)));
}

// mesh_t

TEST(mathlib, meshCreate) {
    const vector<vec3> poly1 {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    const vector<vec3> poly2 {
        { 64,0,0 },
        { 64,64,0 },
        { 128,64,0 },
        { 128,0,0 }
    };
    const vector<vector<vec3>> polys { poly1, poly2 };
    
    const mesh_t m = buildMesh(polys);
    ASSERT_EQ(6, m.verts.size());
    ASSERT_EQ(2, m.faces.size());
    ASSERT_EQ(polys, meshToFaces(m));
}

TEST(mathlib, meshFixTJuncs) {
    /*
     
     poly1
     
   x=0 x=64 x=128
     
     |---|--| y=64  poly2
     |   +--| y=32
     |---|--| y=0   poly3
     
     poly1 should get a vertex inserted at the +
     
     */
    const vector<vec3> poly1 {
        { 0,0,0 },
        { 0,64,0 },
        { 64,64,0 },
        { 64,0,0 }
    };
    const vector<vec3> poly2 {
        { 64,32,0 },
        { 64,64,0 },
        { 128,64,0 },
        { 128,32,0 }
    };
    const vector<vec3> poly3 {
        { 64,0,0 },
        { 64,32,0 },
        { 128,32,0 },
        { 128,0,0 }
    };
    
    const vector<vector<vec3>> polys { poly1, poly2, poly3 };
    
    mesh_t m = buildMesh(polys);
    ASSERT_EQ(8, m.verts.size());
    ASSERT_EQ(3, m.faces.size());
    ASSERT_EQ(polys, meshToFaces(m));
    
    cleanupMesh(m);
    
    const auto newFaces = meshToFaces(m);
#if 0
    EXPECT_NE(poly1, newFaces.at(0));
#endif
    EXPECT_EQ(poly2, newFaces.at(1));
    EXPECT_EQ(poly3, newFaces.at(2));
}

// qvec

TEST(mathlib, qvec_expand) {
    const qvec2f test(1,2);
    const qvec4f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
    EXPECT_EQ(0, test2[2]);
    EXPECT_EQ(0, test2[3]);
}

TEST(mathlib, qvec_contract) {
    const qvec4f test(1,2,0,0);
    const qvec2f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvec_copy) {
    const qvec2f test(1,2);
    const qvec2f test2(test);
    
    EXPECT_EQ(1, test2[0]);
    EXPECT_EQ(2, test2[1]);
}

TEST(mathlib, qvec_constructor_0) {
    const qvec2f test;
    EXPECT_EQ(0, test[0]);
    EXPECT_EQ(0, test[1]);
}

TEST(mathlib, qvec_constructor_1) {
    const qvec2f test(42);
    EXPECT_EQ(42, test[0]);
    EXPECT_EQ(42, test[1]);
}

TEST(mathlib, qvec_constructor_fewer) {
    const qvec4f test(1,2,3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
    EXPECT_EQ(3, test[2]);
    EXPECT_EQ(0, test[3]);
}

TEST(mathlib, qvec_constructor_extra) {
    const qvec2f test(1,2,3);
    EXPECT_EQ(1, test[0]);
    EXPECT_EQ(2, test[1]);
}

// aabb3f

TEST(mathlib, aabb_basic) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    EXPECT_EQ(qvec3f(1,1,1), b1.mins());
    EXPECT_EQ(qvec3f(10,10,10), b1.maxs());
    EXPECT_EQ(qvec3f(9,9,9), b1.size());
}

TEST(mathlib, aabb_grow) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));

    EXPECT_EQ(aabb3f(qvec3f(0,0,0), qvec3f(11,11,11)), b1.grow(qvec3f(1,1,1)));
}

TEST(mathlib, aabb_unionwith) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    const aabb3f b2(qvec3f(11,11,11), qvec3f(12,12,12));
    
    EXPECT_EQ(aabb3f(qvec3f(1,1,1), qvec3f(12,12,12)), b1.unionWith(b2));
}

TEST(mathlib, aabb_expand) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    EXPECT_EQ(b1, b1.expand(qvec3f(1,1,1)));
    EXPECT_EQ(b1, b1.expand(qvec3f(5,5,5)));
    EXPECT_EQ(b1, b1.expand(qvec3f(10,10,10)));
    
    const aabb3f b2(qvec3f(1,1,1), qvec3f(100,10,10));
    EXPECT_EQ(b2, b1.expand(qvec3f(100,10,10)));
    
    const aabb3f b3(qvec3f(0,1,1), qvec3f(10,10,10));
    EXPECT_EQ(b3, b1.expand(qvec3f(0,1,1)));
}

TEST(mathlib, aabb_disjoint) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const aabb3f yes1(qvec3f(-1,-1,-1), qvec3f(0,0,0));
    const aabb3f yes2(qvec3f(11,1,1), qvec3f(12,10,10));
    
    const aabb3f no1(qvec3f(-1,-1,-1), qvec3f(1,1,1));
    const aabb3f no2(qvec3f(10,10,10), qvec3f(10.5,10.5,10.5));
    const aabb3f no3(qvec3f(5,5,5), qvec3f(100,6,6));
    
    EXPECT_TRUE(b1.disjoint(yes1));
    EXPECT_TRUE(b1.disjoint(yes2));
    EXPECT_FALSE(b1.disjoint(no1));
    EXPECT_FALSE(b1.disjoint(no2));
    EXPECT_FALSE(b1.disjoint(no3));
    
    EXPECT_FALSE(b1.intersectWith(yes1).valid);
    EXPECT_FALSE(b1.intersectWith(yes2).valid);
    
    // these intersections are single points
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(1,1,1), qvec3f(1,1,1))), b1.intersectWith(no1));
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(10,10,10), qvec3f(10,10,10))), b1.intersectWith(no2));
    
    // an intersection with a volume
    EXPECT_EQ(aabb3f::intersection_t(aabb3f(qvec3f(5,5,5), qvec3f(10,6,6))), b1.intersectWith(no3));
}

TEST(mathlib, aabb_contains) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const aabb3f yes1(qvec3f(1,1,1), qvec3f(2,2,2));
    const aabb3f yes2(qvec3f(9,9,9), qvec3f(10,10,10));
    
    const aabb3f no1(qvec3f(-1,1,1), qvec3f(2,2,2));
    const aabb3f no2(qvec3f(9,9,9), qvec3f(10.5,10,10));
    
    EXPECT_TRUE(b1.contains(yes1));
    EXPECT_TRUE(b1.contains(yes2));
    EXPECT_FALSE(b1.contains(no1));
    EXPECT_FALSE(b1.contains(no2));
}

TEST(mathlib, aabb_containsPoint) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(10,10,10));
    
    const qvec3f yes1(1,1,1);
    const qvec3f yes2(2,2,2);
    const qvec3f yes3(10,10,10);
    
    const qvec3f no1(0,0,0);
    const qvec3f no2(1,1,0);
    const qvec3f no3(10.1,10.1,10.1);
    
    EXPECT_TRUE(b1.containsPoint(yes1));
    EXPECT_TRUE(b1.containsPoint(yes2));
    EXPECT_TRUE(b1.containsPoint(yes3));
    EXPECT_FALSE(b1.containsPoint(no1));
    EXPECT_FALSE(b1.containsPoint(no2));
    EXPECT_FALSE(b1.containsPoint(no3));
}

TEST(mathlib, aabb_create_invalid) {
    const aabb3f b1(qvec3f(1,1,1), qvec3f(-1,-1,-1));
    const aabb3f fixed(qvec3f(1,1,1), qvec3f(1,1,1));
    
    EXPECT_EQ(fixed, b1);
    EXPECT_EQ(qvec3f(0,0,0), b1.size());
}

// octree

TEST(mathlib, octree_basic) {
    std::mt19937 engine(0);
    std::uniform_int_distribution<> dis(-4096, 4096);
    
    const qvec3f boxsize(64,64,64);
    const int N = 20000;
    
    // generate some objects
    vector<pair<aabb3f, int>> objs;
    for (int i=0; i<N; i++) {
        int x = dis(engine);
        int y = dis(engine);
        int z = dis(engine);
        qvec3f center(x,y,z);
        qvec3f mins = center - boxsize;
        qvec3f maxs = center + boxsize;
        
        aabb3f bbox(mins, maxs);
        objs.push_back(make_pair(bbox, i));
    }
    
    // build octree
    const double insert_start = I_FloatTime();
    auto octree = makeOctree(objs);
    const double insert_end = I_FloatTime();
    printf("inserting %d cubes took %f seconds\n", N, (insert_end - insert_start));
    
    // query for objects overlapping objs[0]'s bbox
    const double exhaustive_query_start = I_FloatTime();
    vector<vector<int>> objsTouchingObjs;
    for (int i=0; i<N; i++) {
        const aabb3f obj_iBBox = objs[i].first;
        
        vector<int> objsTouchingObj_i;
        for (int j=0; j<N; j++) {
            if (!obj_iBBox.disjoint(objs[j].first)) {
                objsTouchingObj_i.push_back(objs[j].second);
            }
        }
        objsTouchingObjs.push_back(objsTouchingObj_i);
    }
    const double exhaustive_query_end = I_FloatTime();
    printf("exhaustive query took %f ms\n", 1000.0 * (exhaustive_query_end - exhaustive_query_start));
    
    // now repeat the same query using the octree
    const double octree_query_start = I_FloatTime();
    vector<vector<int>> objsTouchingObjs_octree;
    for (int i=0; i<N; i++) {
        const aabb3f obj_iBBox = objs[i].first;
        
        vector<int> objsTouchingObj_i = octree.queryTouchingBBox(obj_iBBox);
        objsTouchingObjs_octree.push_back(objsTouchingObj_i);
    }
    const double octree_query_end = I_FloatTime();
    printf("octree query took %f ms\n", 1000.0 * (octree_query_end - octree_query_start));
    
    // compare result
    for (int i=0; i<N; i++) {
        vector<int> &objsTouchingObj_i = objsTouchingObjs[i];
        vector<int> &objsTouchingObj_i_octree = objsTouchingObjs_octree[i];
        
        std::sort(objsTouchingObj_i.begin(), objsTouchingObj_i.end());
        std::sort(objsTouchingObj_i_octree.begin(), objsTouchingObj_i_octree.end());
        EXPECT_EQ(objsTouchingObj_i, objsTouchingObj_i_octree);
    }
}
