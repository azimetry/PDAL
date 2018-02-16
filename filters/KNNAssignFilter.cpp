/******************************************************************************
* Copyright (c) 2017, Hobu Inc., info@hobu.co
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include "KNNAssignFilter.hpp"

#include <pdal/pdal_macros.hpp>
#include <pdal/PipelineManager.hpp>
#include <pdal/StageFactory.hpp>
#include <pdal/util/ProgramArgs.hpp>

#include "private/DimRange.hpp"

#include <iostream>
#include <utility>
namespace pdal
{

static PluginInfo const s_info = PluginInfo(
    "filters.knnassign",
    "Re-assign some point attributes based KNN voting",
    "http://pdal.io/stages/filters.knnassign.html" );

CREATE_STATIC_PLUGIN(1, 0, KNNAssignFilter, Filter, s_info)

KNNAssignFilter::KNNAssignFilter() : m_dim(Dimension::Id::Classification)
{}


KNNAssignFilter::~KNNAssignFilter()
{}


void KNNAssignFilter::addArgs(ProgramArgs& args)
{
    args.add("domain", "Selects which points will be subject to KNN-based assignmenassignment",
        m_domainSpec);
    args.add("k", "Number of nearest neighbors to consult",
        m_k).setPositional();
    //args.add("dimension", "Dimension on to be updated", m_dimName).setPositional();
    Arg& candidate = args.add("candidate", "candidate file name",
        m_candidateFile);
}

void KNNAssignFilter::initialize()
{
    for (auto const& r : m_domainSpec)
    {
        try
        {
            DimRange range;
            range.parse(r);
            m_domain.push_back(range);
        }
        catch (const DimRange::error& err)
        {
            throwError("Invalid 'domain' option: '" + r + "': " + err.what());
        }
    }
    if (m_k < 1)
        throwError("Invalid 'k' option: " + std::to_string(m_k) +  ", must be > 0");
}
void KNNAssignFilter::prepared(PointTableRef table)
{
    PointLayoutPtr layout(table.layout());

    for (auto& r : m_domain)
    {
        r.m_id = layout->findDim(r.m_name);
        if (r.m_id == Dimension::Id::Unknown)
            throwError("Invalid dimension name in 'domain' option: '" +
                r.m_name + "'.");
    }
    std::sort(m_domain.begin(), m_domain.end());
    //m_dim = layout->findDim(m_dimName);

    //if (m_dim == Dimension::Id::Unknown)
    //    throwError("Dimension '" + m_dimName + "' not found.");
}

void KNNAssignFilter::doOneNoDomain(PointRef &point, PointRef &temp, KD3Index &kdi)
{
    std::vector<PointId> iSrc = kdi.neighbors(point, m_k);
    double thresh = iSrc.size()/2.0;
    //std::cout << "iSrc.size() " << iSrc.size() << " thresh " << thresh << std::endl;

    // vote NNs
    std::map<double, unsigned int> counts;
    for (PointId id : iSrc)
    {
        temp.setPointId(id);
        double votefor = temp.getFieldAs<double>(m_dim);
        counts[votefor]++;
    }

    // pick winner of the vote
    auto pr = *std::max_element(counts.begin(), counts.end(),
        [](const std::pair<int, int>& p1, const std::pair<int, int>& p2) {
        return p1.second < p2.second; });

    // update point
    auto oldclass = point.getFieldAs<double>(m_dim);
    auto newclass = pr.first;
    //std::cout << oldclass << " --> " << newclass << " count " << pr.second << std::endl;
    if (pr.second > thresh && oldclass != newclass)
    {    
        point.setField(m_dim, newclass); 
    }
}

bool KNNAssignFilter::doOne(PointRef& point, PointRef &temp, KD3Index &kdi)
{   // update point.  kdi and temp both reference the NN point cloud

    if (m_domain.empty())  // No domain, process all points
        doOneNoDomain(point, temp, kdi);
        
    for (DimRange& r : m_domain)
    {   // process only points that satisfy a domain condition
        if (r.valuePasses(point.getFieldAs<double>(r.m_id)))
        {
            doOneNoDomain(point, temp, kdi);
            break;
        }
    }
    return true;
}

PointViewPtr KNNAssignFilter::loadSet(const std::string& filename,
    PointTable& table)
{
    PipelineManager mgr;

    Stage& reader = mgr.makeReader(filename, "");
    reader.prepare(table);
    PointViewSet viewSet = reader.execute(table);
    assert(viewSet.size() == 1);
    return *viewSet.begin();
}

void KNNAssignFilter::filter(PointView& view)
{
    PointRef point_src(view, 0);
    if (m_candidateFile.empty())
    {   // No candidate file so NN comes from src file
        KD3Index kdiSrc(view);
        kdiSrc.build();
        PointRef point_nn(view, 0);
        for (PointId id = 0; id < view.size(); ++id)
        {
            point_src.setPointId(id);
            doOne(point_src, point_nn, kdiSrc);
        }
    }
    else
    {   // NN comes from candidate file
        PointTable candTable;
        PointViewPtr candView = loadSet(m_candidateFile, candTable);
        KD3Index kdiCand(*candView);
        kdiCand.build();
        PointRef point_nn(*candView, 0);
        for (PointId id = 0; id < view.size(); ++id)
        {
            point_src.setPointId(id);
            doOne(point_src, point_nn, kdiCand);
        }
    }
}

} // namespace pdal

