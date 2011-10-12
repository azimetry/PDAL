/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
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

#include <pdal/drivers/faux/Reader.hpp>

#include <pdal/drivers/faux/Iterator.hpp>
#include <pdal/PointBuffer.hpp>

#include <boost/algorithm/string.hpp>


namespace pdal { namespace drivers { namespace faux {


static Reader::Mode string2mode(const std::string& str)
{
    if (boost::iequals(str, "constant")) return Reader::Constant;
    if (boost::iequals(str, "random")) return Reader::Random;
    if (boost::iequals(str, "ramp")) return Reader::Ramp;
    throw pdal_error("invalid Mode option: " + str);
}


Reader::Reader(const Options& options)
    : pdal::Reader(options)
    , m_bounds(options.getValueOrThrow<Bounds<double> >("bounds"))
    , m_numPoints(options.getValueOrThrow<boost::uint64_t>("num_points"))
    , m_mode(string2mode(options.getValueOrThrow<std::string>("mode")))
{
    return;
}


Reader::Reader(const Bounds<double>& bounds, boost::uint64_t numPoints, Mode mode)
    : pdal::Reader(Options::none())
    , m_bounds(bounds)
    , m_numPoints(numPoints)
    , m_mode(mode)
{
    return;
}

Reader::Reader(const Bounds<double>& bounds, boost::uint64_t numPoints, Mode mode, const std::vector<Dimension>& dimensions)
    : pdal::Reader( Options::none())
    , m_bounds(bounds)
    , m_numPoints(numPoints)
    , m_mode(mode)
{
    if (dimensions.size() == 0)
    {
        throw; // BUG
    }

    m_dimensions = dimensions;

    return;
}


void Reader::initialize()
{
    pdal::Reader::initialize();

    Schema& schema = getSchemaRef();

    if (m_dimensions.size() == 0)
    {
        // these are the default dimensions we use
        Dimension dimx(DimensionId::X_f64);
        dimx.setFlags(Dimension::IsAdded & Dimension::IsWritten);
        schema.appendDimension(dimx);

        Dimension dimy(DimensionId::Y_f64);
        dimy.setFlags(Dimension::IsAdded & Dimension::IsWritten);
        schema.appendDimension(dimy);

        Dimension dimz(DimensionId::Z_f64);
        dimz.setFlags(Dimension::IsAdded & Dimension::IsWritten);
        schema.appendDimension(dimz);

        Dimension dimt(DimensionId::Time_u64);
        dimt.setFlags(Dimension::IsAdded & Dimension::IsWritten);
        schema.appendDimension(dimt);
    }
    else
    {
        for (boost::uint32_t i=0; i<m_dimensions.size(); i++)
        {
            const Dimension& dim = m_dimensions[i];
            schema.appendDimension(dim);
        }
    }

    setNumPoints(m_numPoints);
    setPointCountType(PointCount_Fixed);

    setBounds(m_bounds);
}


const Options Reader::getDefaultOptions() const
{
    Options options;
    return options;
}


Reader::Mode Reader::getMode() const
{
    return m_mode;
}


pdal::StageSequentialIterator* Reader::createSequentialIterator() const
{
    return new SequentialIterator(*this);
}


pdal::StageRandomIterator* Reader::createRandomIterator() const
{
    return new RandomIterator(*this);
}


boost::uint32_t Reader::processBuffer(PointBuffer& data, boost::uint64_t index) const
{
    const Schema& schema = data.getSchema();

    if (schema.getDimensions().size() != 4)
        throw not_yet_implemented("need to add ability to read from arbitrary fields");

    // make up some data and put it into the buffer

    // how many are they asking for?
    boost::uint64_t numPointsWanted = data.getCapacity();

    // we can only give them as many as we have left
    boost::uint64_t numPointsAvailable = getNumPoints() - index;
    if (numPointsAvailable < numPointsWanted)
        numPointsWanted = numPointsAvailable;

    const Bounds<double>& bounds = getBounds(); 
    const std::vector< Range<double> >& dims = bounds.dimensions();
    const double minX = dims[0].getMinimum();
    const double maxX = dims[0].getMaximum();
    const double minY = dims[1].getMinimum();
    const double maxY = dims[1].getMaximum();
    const double minZ = dims[2].getMinimum();
    const double maxZ = dims[2].getMaximum();
    
    const double numDeltas = (double)getNumPoints() - 1.0;
    const double delX = (maxX - minX) / numDeltas;
    const double delY = (maxY - minY) / numDeltas;
    const double delZ = (maxZ - minZ) / numDeltas;

    const int offsetT = schema.getDimensionIndex(DimensionId::Time_u64);
    const int offsetX = schema.getDimensionIndex(DimensionId::X_f64);
    const int offsetY = schema.getDimensionIndex(DimensionId::Y_f64);
    const int offsetZ = schema.getDimensionIndex(DimensionId::Z_f64);

    boost::uint64_t time = index;
    
    const Reader::Mode mode = getMode();

    boost::uint32_t cnt = 0;
    data.setNumPoints(0);

    for (boost::uint32_t pointIndex=0; pointIndex<numPointsWanted; pointIndex++)
    {
        double x;
        double y;
        double z;
        switch (mode)
        {
        case Reader::Random:
            x = Utils::random(minX, maxX);
            y = Utils::random(minY, maxY);
            z = Utils::random(minZ, maxZ);
            break;
        case Reader::Constant:
            x = minX;
            y = minY;
            z = minZ;
            break;
        case Reader::Ramp:
            x = minX + delX * pointIndex;
            y = minY + delY * pointIndex;
            z = minZ + delZ * pointIndex;
            break;
        default:
            throw pdal_error("invalid mode in FauxReader");
            break;
        }

        data.setField<double>(pointIndex, offsetX, x);
        data.setField<double>(pointIndex, offsetY, y);
        data.setField<double>(pointIndex, offsetZ, z);
        data.setField<boost::uint64_t>(pointIndex, offsetT, time);

        ++time;
        
        ++cnt;
        data.setNumPoints(cnt);
        assert(cnt <= data.getCapacity());
    }
    
    return cnt;
}


boost::property_tree::ptree Reader::toPTree() const
{
    boost::property_tree::ptree tree = pdal::Reader::toPTree();

    // add stuff here specific to this stage type

    return tree;
}


} } } // namespaces
