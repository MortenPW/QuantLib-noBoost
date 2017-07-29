/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "utilities.hpp"
#include <ql/time/businessdayconvention.hpp>
#include <ql/time/daycounter.hpp>
#include <ql/time/calendars/southafrica.hpp>
#include <ql/time/period.hpp>

using namespace QuantLib;


namespace {
    struct SingleCase {
        SingleCase(const Calendar& calendar,
                   const BusinessDayConvention& convention,
                   const Date& start,
                   const Period& period,
                   const bool endOfMonth,
                   Date result)
        : calendar_(calendar), convention_(convention), start_(start),
          period_(period), endOfMonth_(endOfMonth), result_(result) {}
        Calendar calendar_;
        BusinessDayConvention convention_;
        Date start_;
        Period period_;
        bool endOfMonth_;
        Date result_;
    };

}

TEST_CASE("BusinessDayConvention_Conventions", "[BusinessDayConvention]") {

    INFO("Testing business day conventions...");

    SingleCase testCases[] = {
        // Following
        SingleCase(SouthAfrica(), Following, Date(3,February,2015), Period(1,Months), false, Date(3,March,2015)),
        SingleCase(SouthAfrica(), Following, Date(3,February,2015), Period(4,Days), false, Date(9,February,2015)),
        SingleCase(SouthAfrica(), Following, Date(31,January,2015), Period(1,Months), true, Date(27,February,2015)),
        SingleCase(SouthAfrica(), Following, Date(31,January,2015), Period(1,Months), false, Date(2,March,2015)),

        //ModifiedFollowing
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(3,February,2015), Period(1,Months), false, Date(3,March,2015)),
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(3,February,2015), Period(4,Days), false, Date(9,February,2015)),
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(31,January,2015), Period(1,Months), true, Date(27,February,2015)),
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(31,January,2015), Period(1,Months), false, Date(27,February,2015)),
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(25,March,2015), Period(1,Months), false, Date(28,April,2015)),
        SingleCase(SouthAfrica(), ModifiedFollowing, Date(7,February,2015), Period(1,Months), false, Date(9,March,2015)),

        //Preceding
        SingleCase(SouthAfrica(), Preceding, Date(3,March,2015), Period(-1,Months), false, Date(3,February,2015)),
        SingleCase(SouthAfrica(), Preceding, Date(3,February,2015), Period(-2,Days), false, Date(30,January,2015)),
        SingleCase(SouthAfrica(), Preceding, Date(1,March,2015), Period(-1,Months), true, Date(30,January,2015)),
        SingleCase(SouthAfrica(), Preceding, Date(1,March,2015), Period(-1,Months), false, Date(30,January,2015)),

        //ModifiedPreceding
        SingleCase(SouthAfrica(), ModifiedPreceding, Date(3,March,2015), Period(-1,Months), false, Date(3,February,2015)),
        SingleCase(SouthAfrica(), ModifiedPreceding, Date(3,February,2015), Period(-2,Days), false, Date(30,January,2015)),
        SingleCase(SouthAfrica(), ModifiedPreceding, Date(1,March,2015), Period(-1,Months), true, Date(2,February,2015)),
        SingleCase(SouthAfrica(), ModifiedPreceding, Date(1,March,2015), Period(-1,Months), false, Date(2,February,2015)),

        //Unadjusted
        SingleCase(SouthAfrica(), Unadjusted, Date(3,February,2015), Period(1,Months), false, Date(3,March,2015)),
        SingleCase(SouthAfrica(), Unadjusted, Date(3,February,2015), Period(4,Days), false, Date(9,February,2015)),
        SingleCase(SouthAfrica(), Unadjusted, Date(31,January,2015), Period(1,Months), true, Date(27,February,2015)),
        SingleCase(SouthAfrica(), Unadjusted, Date(31,January,2015), Period(1,Months), false, Date(28,February,2015)),

        //HalfMonthModifiedFollowing
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(3,February,2015), Period(1,Months), false, Date(3,March,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(3,February,2015), Period(4,Days), false, Date(9,February,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(31,January,2015), Period(1,Months), true, Date(27,February,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(31,January,2015), Period(1,Months), false, Date(27,February,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(3,January,2015), Period(1,Weeks), false, Date(12,January,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(21,March,2015), Period(1,Weeks), false, Date(30,March,2015)),
        SingleCase(SouthAfrica(), HalfMonthModifiedFollowing, Date(7,February,2015), Period(1,Months), false, Date(9,March,2015)),

        //Nearest
        SingleCase(SouthAfrica(), Nearest, Date(3,February,2015), Period(1,Months), false, Date(3,March,2015)),
        SingleCase(SouthAfrica(), Nearest, Date(3,February,2015), Period(4,Days), false, Date(9,February,2015)),
        SingleCase(SouthAfrica(), Nearest, Date(16,April,2015), Period(1,Months), false, Date(15,May,2015)),
        SingleCase(SouthAfrica(), Nearest, Date(17,April,2015), Period(1,Months), false, Date(18,May,2015)),
        SingleCase(SouthAfrica(), Nearest, Date(4,March,2015), Period(1,Months), false, Date(2,April,2015)),
        SingleCase(SouthAfrica(), Nearest, Date(2,April,2015), Period(1,Months), false, Date(4,May,2015))
    };

    Size n = sizeof(testCases)/sizeof(SingleCase);
    for (Size i=0; i<n; i++) {
        Calendar calendar(testCases[i].calendar_);
        Date result = calendar.advance(
            testCases[i].start_,
            testCases[i].period_,
            testCases[i].convention_,
            testCases[i].endOfMonth_);

        if (result != testCases[i].result_)
		FAIL_CHECK( "\ncase " << i << ":\n" //<< j << " ("<< desc << "): "
                            << "start date: " << testCases[i].start_ << "\n"
                            << "calendar: " << calendar << "\n"
                            << "period: " << testCases[i].period_ << ", end of month: " << testCases[i].endOfMonth_ << "\n"
                            << "convention: " << testCases[i].convention_ << "\n"
                            << "expected: " << testCases[i].result_ << " vs. actual: " << result);
    }
}
