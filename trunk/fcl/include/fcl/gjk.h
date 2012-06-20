/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/** \author Jia Pan */

#ifndef FCL_GJK_H
#define FCL_GJK_H

#include "fcl/geometric_shapes.h"
#include "fcl/matrix_3f.h"
#include "fcl/transform.h"

namespace fcl
{


Vec3f getSupport(const ShapeBase* shape, const Vec3f& dir); 

struct MinkowskiDiff
{
  const ShapeBase* shapes[2];
  Matrix3f toshape1;
  SimpleTransform toshape0;

  MinkowskiDiff() { }

  inline Vec3f support0(const Vec3f& d) const
  {
    return getSupport(shapes[0], d);
  }

  inline Vec3f support1(const Vec3f& d) const
  {
    return toshape0.transform(getSupport(shapes[1], toshape1 * d));
  }

  inline Vec3f support(const Vec3f& d) const
  {
    return support0(d) - support1(-d);
  }

  inline Vec3f support(const Vec3f& d, size_t index) const
  {
    if(index)
      return support1(d);
    else
      return support0(d);
  }

};

namespace details
{

BVH_REAL projectOrigin(const Vec3f& a, const Vec3f& b, BVH_REAL* w, size_t& m);

BVH_REAL projectOrigin(const Vec3f& a, const Vec3f& b, const Vec3f& c, BVH_REAL* w, size_t& m);

BVH_REAL projectOrigin(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, BVH_REAL* w, size_t& m);

}

static const BVH_REAL GJK_EPS = 0.000001;
static const size_t GJK_MAX_ITERATIONS = 128;

struct GJK
{
  struct SimplexV
  {
    Vec3f d; // support direction
    Vec3f w; // support vector
  };

  struct Simplex
  {
    SimplexV* c[4]; // simplex vertex
    BVH_REAL p[4]; // weight
    size_t rank; // size of simplex (number of vertices)
  };

  enum Status {Valid, Inside, Failed};

  MinkowskiDiff shape;
  Vec3f ray;
  BVH_REAL distance;
  Simplex simplices[2];


  GJK() { initialize(); }
  
  void initialize()
  {
    ray = Vec3f();
    nfree = 0;
    status = Failed;
    current = 0;
    distance = 0.0;
  }

  Status evaluate(const MinkowskiDiff& shape_, const Vec3f& guess);

  void getSupport(const Vec3f& d, SimplexV& sv) const;

  void removeVertex(Simplex& simplex);

  void appendVertex(Simplex& simplex, const Vec3f& v);

  bool encloseOrigin();

  inline Simplex* getSimplex() const
  {
    return simplex;
  }
  
private:
  SimplexV store_v[4];
  SimplexV* free_v[4];
  size_t nfree;
  size_t current;
  Simplex* simplex;
  Status status;
};


static const size_t EPA_MAX_FACES = 128;
static const size_t EPA_MAX_VERTICES = 64;
static const BVH_REAL EPA_EPS = 0.000001;
static const size_t EPA_MAX_ITERATIONS = 255;

struct EPA
{
  typedef GJK::SimplexV SimplexV;
  struct SimplexF
  {
    Vec3f n;
    BVH_REAL d;
    SimplexV* c[3]; // a face has three vertices
    SimplexF* f[3]; // a face has three adjacent faces
    SimplexF* l[2]; // the pre and post faces in the list
    size_t e[3];
    size_t pass;
  };

  struct SimplexList
  {
    SimplexF* root;
    size_t count;
    SimplexList() : root(NULL), count(0) {}
    void append(SimplexF* face)
    {
      face->l[0] = NULL;
      face->l[1] = root;
      if(root) root->l[0] = face;
      root = face;
      ++count;
    }

    void remove(SimplexF* face)
    {
      if(face->l[1]) face->l[1]->l[0] = face->l[0];
      if(face->l[0]) face->l[0]->l[1] = face->l[1];
      if(face == root) root = face->l[1];
      --count;
    }
  };

  static inline void bind(SimplexF* fa, size_t ea, SimplexF* fb, size_t eb)
  {
    fa->e[ea] = eb; fa->f[ea] = fb;
    fb->e[eb] = ea; fb->f[eb] = fa;
  }

  struct SimplexHorizon
  {
    SimplexF* cf; // current face in the horizon
    SimplexF* ff; // first face in the horizon
    size_t nf; // number of faces in the horizon
    SimplexHorizon() : cf(NULL), ff(NULL), nf(0) {}
  };

  enum Status {Valid, Touching, Degenerated, NonConvex, InvalidHull, OutOfFaces, OutOfVertices, AccuracyReached, FallBack, Failed};
  
  Status status;
  GJK::Simplex result;
  Vec3f normal;
  BVH_REAL depth;
  SimplexV sv_store[EPA_MAX_VERTICES];
  SimplexF fc_store[EPA_MAX_FACES];
  size_t nextsv;
  SimplexList hull, stock;

  EPA()
  {
    initialize();
  }

  void initialize()
  {
    status = Failed;
    normal = Vec3f(0, 0, 0);
    depth = 0;
    nextsv = 0;
    for(size_t i = 0; i < EPA_MAX_FACES; ++i)
      stock.append(&fc_store[EPA_MAX_FACES-i-1]);
  }

  bool getEdgeDist(SimplexF* face, SimplexV* a, SimplexV* b, BVH_REAL& dist);

  SimplexF* newFace(SimplexV* a, SimplexV* b, SimplexV* c, bool forced);

  /** \brief Find the best polytope face to split */
  SimplexF* findBest();

  Status evaluate(GJK& gjk, const Vec3f& guess);

  /** \brief the goal is to add a face connecting vertex w and face edge f[e] */
  bool expand(size_t pass, SimplexV* w, SimplexF* f, size_t e, SimplexHorizon& horizon);  
};

template<typename S1, typename S2>
bool shapeDistance2(const S1& s1, const SimpleTransform& tf1,
                    const S2& s2, const SimpleTransform& tf2,
                    BVH_REAL* distance)
{
  Vec3f guess(1, 0, 0);
  MinkowskiDiff shape;
  shape.shapes[0] = &s1;
  shape.shapes[1] = &s2;
  shape.toshape1 = tf2.getRotation().transposeTimes(tf1.getRotation());
  shape.toshape0 = tf1.inverseTimes(tf2);

  GJK gjk;
  GJK::Status gjk_status = gjk.evaluate(shape, -guess);
  if(gjk_status == GJK::Valid)
  {
    Vec3f w0, w1;
    for(size_t i = 0; i < gjk.getSimplex()->rank; ++i)
    {
      BVH_REAL p = gjk.getSimplex()->p[i];
      w0 += shape.support(gjk.getSimplex()->c[i]->d, 0) * p;
      w1 += shape.support(-gjk.getSimplex()->c[i]->d, 1) * p;
    }

    *distance = (w0 - w1).length();
    return true;
  }
  else
  {
    *distance = -1;
    return false;
  }
}

template<typename S1, typename S2>
bool shapeIntersect2(const S1& s1, const SimpleTransform& tf1,
                     const S2& s2, const SimpleTransform& tf2,
                     Vec3f* contact_points = NULL, BVH_REAL* penetration_depth = NULL, Vec3f* normal = NULL)
{
  Vec3f guess(1, 0, 0);
  MinkowskiDiff shape;
  shape.shapes[0] = &s1;
  shape.shapes[1] = &s2;
  shape.toshape1 = tf2.getRotation().transposeTimes(tf1.getRotation());
  shape.toshape0 = tf1.inverseTimes(tf2);
  
  GJK gjk;
  GJK::Status gjk_status = gjk.evaluate(shape, -guess);
  switch(gjk_status)
  {
  case GJK::Inside:
    {
      EPA epa;
      EPA::Status epa_status = epa.evaluate(gjk, -guess);
      if(epa_status != EPA::Failed)
      {
        Vec3f w0;
        for(size_t i = 0; i < epa.result.rank; ++i)
        {
          w0 += shape.support(epa.result.c[i]->d, 0) * epa.result.p[i];
        }
        if(penetration_depth) *penetration_depth = -epa.depth;
        if(normal) *normal = -epa.normal;
        if(contact_points) *contact_points = tf1.transform(w0 - epa.normal*(epa.depth *0.5));
        return true;
      }
      else return false;
    }
    break;
  default:
    ;
  }

  return false;
}

}


#endif
