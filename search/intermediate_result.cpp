#include "intermediate_result.hpp"

#include "../storage/country_info.hpp"

#include "../indexer/classificator.hpp"
#include "../indexer/feature.hpp"
#include "../indexer/feature_utils.hpp"
#include "../indexer/mercator.hpp"
#include "../indexer/scales.hpp"

#include "../geometry/angles.hpp"
#include "../geometry/distance_on_sphere.hpp"

#include "../base/string_utils.hpp"
#include "../base/logging.hpp"


namespace search
{
namespace impl
{

IntermediateResult::IntermediateResult(m2::RectD const & viewportRect, m2::PointD const & pos,
                                       FeatureType const & f, m2::PointD const & center, uint8_t rank,
                                       string const & displayName, string const & fileName)
  : m_types(f),
    m_str(displayName),
    m_resultType(RESULT_FEATURE),
    m_center(center), m_searchRank(rank)
{
  ASSERT_GREATER(m_types.Size(), 0, ());

  // get region info
  if (!fileName.empty())
    m_region.SetName(fileName);
  else
    m_region.SetPoint(m_center);

  CalcCommonParams(viewportRect, pos);
}

IntermediateResult::IntermediateResult(m2::RectD const & viewportRect, m2::PointD const & pos,
                                       double lat, double lon, double precision)
  : m_str("(" + strings::to_string(lat) + ", " + strings::to_string(lon) + ")"),
    m_center(m2::PointD(MercatorBounds::LonToX(lon), MercatorBounds::LatToY(lat))),
    m_resultType(RESULT_LATLON), m_searchRank(255)
{
  CalcCommonParams(viewportRect, pos);

  // get region info
  m_region.SetPoint(m_center);
}

void IntermediateResult::CalcCommonParams(m2::RectD const & viewportRect, m2::PointD const & pos)
{
  // Check if point is valid (see Query::empty_pos_value).
  if (pos.x > -500 && pos.y > -500)
  {
    ASSERT ( my::between_s(-180.0, 180.0, pos.x), (pos.x) );
    ASSERT ( my::between_s(-180.0, 180.0, pos.y), (pos.y) );

    m_distance = ResultDistance(pos, m_center);
  }
  else
  {
    // empty distance
    m_distance = -1.0;
  }

  m_viewportDistance = ViewportDistance(viewportRect, m_center);
}

IntermediateResult::IntermediateResult(string const & name, int penalty)
  : m_str(name), m_completionString(name + " "),
    // Categories should always be first.
    m_distance(-1000.0),    // smallest distance :)
    m_viewportDistance(0),  // closest to viewport
    m_resultType(RESULT_CATEGORY),
    m_searchRank(255)       // best rank
{
}

/*
bool IntermediateResult::LessOrderF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType != r2.m_resultType)
    return (r1.m_resultType < r2.m_resultType);

  if (r1.m_searchRank != r2.m_searchRank)
    return (r1.m_searchRank > r2.m_searchRank);

  return (r1.m_distance < r2.m_distance);
}
*/

bool IntermediateResult::LessRank(IntermediateResult const & r1, IntermediateResult const & r2)
{
  return (r1.m_searchRank > r2.m_searchRank);
}

bool IntermediateResult::LessDistance(IntermediateResult const & r1, IntermediateResult const & r2)
{
  return (r1.m_distance < r2.m_distance);

  /*
  if (r1.m_distance != r2.m_distance)
    return (r1.m_distance < r2.m_distance);
  else
    return LessRank(r1, r2);
  */
}

bool IntermediateResult::LessViewportDistance(IntermediateResult const & r1, IntermediateResult const & r2)
{
  return (r1.m_viewportDistance < r2.m_viewportDistance);

  /*
  if (r1.m_viewportDistance != r2.m_viewportDistance)
    return (r1.m_viewportDistance < r2.m_viewportDistance);
  else
    return LessRank(r1, r2);
  */
}

Result IntermediateResult::GenerateFinalResult(
    storage::CountryInfoGetter const * pInfo,
    CategoriesT const * pCat) const
{
  storage::CountryInfo info;
  m_region.GetRegion(pInfo, info);

  switch (m_resultType)
  {
  case RESULT_FEATURE:
    return Result(m_str, info.m_name, info.m_flag, GetFeatureType(pCat)
              #ifdef DEBUG
                  + ' ' + strings::to_string(static_cast<int>(m_searchRank))
              #endif
                  ,
                  GetBestType(), feature::GetFeatureViewport(m_types, m_center),
                  m_distance);

  case RESULT_LATLON:
    return Result(m_str, info.m_name, info.m_flag, string(), 0,
                  scales::GetRectForLevel(scales::GetUpperScale(), m_center, 1.0), m_distance);

  default:
    ASSERT_EQUAL ( m_resultType, RESULT_CATEGORY, () );
    return Result(m_str, m_completionString);
  }
}

double IntermediateResult::ResultDistance(m2::PointD const & a, m2::PointD const & b)
{
  return ms::DistanceOnEarth(MercatorBounds::YToLat(a.y), MercatorBounds::XToLon(a.x),
                             MercatorBounds::YToLat(b.y), MercatorBounds::XToLon(b.x));
}

double IntermediateResult::ResultDirection(m2::PointD const & a, m2::PointD const & b)
{
  return ang::AngleTo(a, b);
}

int IntermediateResult::ViewportDistance(m2::RectD const & viewport, m2::PointD const & p)
{
  if (viewport.IsPointInside(p))
    return 0;

  m2::RectD r = viewport;
  r.Scale(3);
  if (r.IsPointInside(p))
    return 1;

  r = viewport;
  r.Scale(5);
  if (r.IsPointInside(p))
    return 2;

  return 3;
}

bool IntermediateResult::StrictEqualF::operator()(IntermediateResult const & r) const
{
  if (m_r.m_resultType == r.m_resultType && m_r.m_resultType == RESULT_FEATURE)
  {
    if (m_r.m_str == r.m_str && m_r.GetBestType() == r.GetBestType())
    {
      // 100.0m - distance between equal features
      return fabs(m_r.m_distance - r.m_distance) < 100.0;
    }
  }

  return false;
}

namespace
{
  uint8_t FirstLevelIndex(uint32_t t)
  {
    uint8_t v;
    VERIFY ( ftype::GetValue(t, 0, v), (t) );
    return v;
  }

  class IsLinearChecker
  {
    static size_t const m_count = 2;
    uint8_t m_index[m_count];

  public:
    IsLinearChecker()
    {
      char const * arr[] = { "highway", "waterway" };
      STATIC_ASSERT ( ARRAY_SIZE(arr) == m_count );

      ClassifObject const * c = classif().GetRoot();
      for (size_t i = 0; i < m_count; ++i)
        m_index[i] = static_cast<uint8_t>(c->BinaryFind(arr[i]).GetIndex());
    }

    bool IsMy(uint8_t ind) const
    {
      for (size_t i = 0; i < m_count; ++i)
        if (ind == m_index[i])
          return true;

      return false;
    }
  };
}

bool IntermediateResult::LessLinearTypesF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType != r2.m_resultType)
    return (r1.m_resultType < r2.m_resultType);

  if (r1.m_str != r2.m_str)
    return (r1.m_str < r2.m_str);

  if (r1.GetBestType() != r2.GetBestType())
    return (r1.GetBestType() < r2.GetBestType());

  // Should stay the best feature, after unique, so add this criteria:

  if (r1.m_searchRank != r2.m_searchRank)
    return (r1.m_searchRank > r2.m_searchRank);
  return (r1.m_distance < r2.m_distance);
}

bool IntermediateResult::EqualLinearTypesF::operator()
          (IntermediateResult const & r1, IntermediateResult const & r2) const
{
  if (r1.m_resultType == r2.m_resultType && r1.m_str == r2.m_str)
  {
    // filter equal linear features
    static IsLinearChecker checker;
    return (r1.GetBestType() == r2.GetBestType() &&
            checker.IsMy(FirstLevelIndex(r1.GetBestType())));
  }

  return false;
}

string IntermediateResult::DebugPrint() const
{
  string res("IntermediateResult: ");
  res += "Name: " + m_str;
  res += "; Type: " + ::DebugPrint(GetBestType());
  res += "; Rank: " + ::DebugPrint(m_searchRank);
  res += "; Viewport distance: " + ::DebugPrint(m_viewportDistance);
  res += "; Distance: " + ::DebugPrint(m_distance);
  return res;
}

string IntermediateResult::GetFeatureType(CategoriesT const * pCat) const
{
  ASSERT_EQUAL(m_resultType, RESULT_FEATURE, ());

  uint32_t const type = GetBestType();
  ASSERT_NOT_EQUAL(type, 0, ());

  if (pCat)
  {
    for (CategoriesT::const_iterator i = pCat->begin(); i != pCat->end(); ++i)
    {
      if (i->second == type)
        return strings::ToUtf8(i->first);
    }
  }

  string s = classif().GetFullObjectName(type);

  // remove ending dummy symbol
  ASSERT ( !s.empty(), () );
  s.resize(s.size()-1);

  // replace separator
  replace(s.begin(), s.end(), '|', '-');
  return s;
}

void IntermediateResult::RegionInfo::GetRegion(
    storage::CountryInfoGetter const * pInfo, storage::CountryInfo & info) const
{
  if (!m_file.empty())
    pInfo->GetRegionInfo(m_file, info);
  else if (m_valid)
    pInfo->GetRegionInfo(m_point, info);
}

}  // namespace search::impl
}  // namespace search
