/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2006 Warren Chou
 Copyright (C) 2007, 2008 StatPro Italia srl

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
#include <ql/quotes/simplequote.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/instruments/varianceswap.hpp>
#include <ql/pricingengines/forward/replicatingvarianceswapengine.hpp>
#include <ql/pricingengines/forward/mcvarianceswapengine.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancecurve.hpp>
#include <ql/termstructures/volatility/equityfx/blackvariancesurface.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/processes/blackscholesprocess.hpp>

using namespace QuantLib;


#define REPORT_FAILURE(greekName, isLong, varStrike, nominal, s, q, r, today, \
                       exDate, v, expected, calculated, error, tolerance) \
    FAIL_CHECK( \
        " variance swap with " \
        << "    underlying value: " << s << "\n" \
        << "    strike:           " << varStrike << "\n" \
        << "    nominal:          " << nominal << "\n" \
        << "    dividend yield:   " << io::rate(q) << "\n" \
        << "    risk-free rate:   " << io::rate(r) << "\n" \
        << "    reference date:   " << today << "\n" \
        << "    maturity:         " << exDate << "\n" \
        << "    volatility:       " << io::volatility(v) << "\n\n" \
        << "    expected   " << greekName << ": " << expected << "\n" \
        << "    calculated " << greekName << ": " << calculated << "\n"\
        << "    error:            " << error << "\n" \
        << "    tolerance:        " << tolerance);


namespace {

    struct MCVarianceSwapData {
        Position::Type type;
        Real varStrike;
        Real nominal;
        Real s;         // spot
        Rate q;         // dividend
        Rate r;         // risk-free rate
        Time t1;        // intermediate time
        Time t;         // time to maturity
        Volatility v1;  // volatility at t1
        Volatility v;   // volatility at t
        Real result;    // result
        Real tol;       // tolerance
    };

    struct ReplicatingVarianceSwapData {
        Position::Type type;
        Real varStrike;
        Real nominal;
        Real s;         // spot
        Rate q;         // dividend
        Rate r;         // risk-free rate
        Time t;         // time to maturity
        Volatility v;   // volatility at t
        Real result;    // result
        Real tol;       // tolerance
    };

    struct Datum {
        Option::Type type;
        Real strike;
        Volatility v;
    };

}


TEST_CASE("VarianceSwap_ReplicatingVarianceSwap", "[VarianceSwap]") {

    INFO("Testing variance swap with replicating cost engine...");

    ReplicatingVarianceSwapData values[] = {

        // data from "A Guide to Volatility and Variance Swaps",
        //   Derman, Kamal & Zou, 1999
        //   with maturity t corrected from 0.25 to 0.246575
        //   corresponding to Jan 1, 1999 to Apr 1, 1999

        //type, varStrike, nominal,     s,    q,    r,        t,    v,  result, tol
        {   Position::Long,      0.04,   50000, 100.0, 0.00, 0.05, 0.246575, 0.20, 0.04189, 1.0e-4}

    };

    Datum replicatingOptionData[] = {

        // data from "A Guide to Volatility and Variance Swaps",
        //   Derman, Kamal & Zou, 1999

        //Option::Type, strike, v
        { Option::Put,   50,  0.30 },
        { Option::Put,   55,  0.29 },
        { Option::Put,   60,  0.28 },
        { Option::Put,   65,  0.27 },
        { Option::Put,   70,  0.26 },
        { Option::Put,   75,  0.25 },
        { Option::Put,   80,  0.24 },
        { Option::Put,   85,  0.23 },
        { Option::Put,   90,  0.22 },
        { Option::Put,   95,  0.21 },
        { Option::Put,  100,  0.20 },
        { Option::Call, 100,  0.20 },
        { Option::Call, 105,  0.19 },
        { Option::Call, 110,  0.18 },
        { Option::Call, 115,  0.17 },
        { Option::Call, 120,  0.16 },
        { Option::Call, 125,  0.15 },
        { Option::Call, 130,  0.14 },
        { Option::Call, 135,  0.13 }
    };

    DayCounter dc = Actual365Fixed();
    Date today = Date::todaysDate();

    std::shared_ptr<SimpleQuote> spot = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<SimpleQuote> qRate = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<YieldTermStructure> qTS = flatRate(today, qRate, dc);
    std::shared_ptr<SimpleQuote> rRate = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<YieldTermStructure> rTS = flatRate(today, rRate, dc);

    for (Size i=0; i<LENGTH(values); i++) {
        Date exDate = today + Integer(values[i].t*365+0.5);
        std::vector<Date> dates(1);
        dates[0] = exDate;

        spot ->setValue(values[i].s);
        qRate->setValue(values[i].q);
        rRate->setValue(values[i].r);

        Size options = LENGTH(replicatingOptionData);
        std::vector<Real> callStrikes, putStrikes, callVols, putVols;

        // Assumes ascending strikes and same min call and max put strikes
        Size j;
        for (j=0; j<options; j++) {
            if (replicatingOptionData[j].type == Option::Call) {
                callStrikes.emplace_back(replicatingOptionData[j].strike);
                callVols.emplace_back(replicatingOptionData[j].v);
            } else if (replicatingOptionData[j].type == Option::Put) {
                putStrikes.emplace_back(replicatingOptionData[j].strike);
                putVols.emplace_back(replicatingOptionData[j].v);
            } else {
                QL_FAIL("unknown option type");
            }
        }

        Matrix vols(options-1, 1);
        std::vector<Real> strikes;
        for (j=0; j<putVols.size(); j++) {
            vols[j][0] = putVols[j];
            strikes.emplace_back(putStrikes[j]);
        }

        for (Size k=1; k<callVols.size(); k++) {
            Size jj = putVols.size()-1;
            vols[jj+k][0] = callVols[k];
            strikes.emplace_back(callStrikes[k]);
        }

        std::shared_ptr<BlackVolTermStructure> volTS = 
            std::make_shared<BlackVarianceSurface>(today, NullCalendar(),
                                 dates, strikes, vols, dc);

        std::shared_ptr<GeneralizedBlackScholesProcess> stochProcess =
                             std::make_shared<BlackScholesMertonProcess>(
                                       Handle<Quote>(spot),
                                       Handle<YieldTermStructure>(qTS),
                                       Handle<YieldTermStructure>(rTS),
                                       Handle<BlackVolTermStructure>(volTS));


        std::shared_ptr<PricingEngine> engine =
                          std::make_shared<ReplicatingVarianceSwapEngine>(stochProcess, 5.0,
                                                            callStrikes,
                                                            putStrikes);

        VarianceSwap varianceSwap(values[i].type,
                                  values[i].varStrike,
                                  values[i].nominal,
                                  today,
                                  exDate);
        varianceSwap.setPricingEngine(engine);

        Real calculated = varianceSwap.variance();
        Real expected = values[i].result;
        Real error = std::fabs(calculated-expected);
        if (error>values[i].tol)
            REPORT_FAILURE("value", values[i].type, values[i].varStrike,
                           values[i].nominal, values[i].s, values[i].q,
                           values[i].r, today, exDate, values[i].v, expected,
                           calculated, error, values[i].tol);
    }
}


TEST_CASE("VarianceSwap_MCVarianceSwap", "[VarianceSwap]") {

    INFO("Testing variance swap with Monte Carlo engine...");

    MCVarianceSwapData values[] = {

        // data from "A Guide to Volatility and Variance Swaps",
        //   Derman, Kamal & Zou, 1999
        //   with maturity t corrected from 0.25 to 0.246575
        //   corresponding to Jan 1, 1999 to Apr 1, 1999

        // exercising code using BlackVarianceCurve because BlackVarianceSurface is unreliable
        // Result should be v*v for arbitrary t1 and v1 (as long as 0<=t1<t and 0<=v1<v)

        //type, varStrike, nominal,     s,    q,    r,  t1,     t,     v1,    v, result, tol
        {   Position::Long,      0.04,   50000, 100.0, 0.00, 0.05, 0.1, 0.246575, 0.1, 0.20,   0.04, 3.0e-4}

    };


    DayCounter dc = Actual365Fixed();
    Date today = Date::todaysDate();

    std::shared_ptr<SimpleQuote> spot = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<SimpleQuote> qRate = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<YieldTermStructure> qTS = flatRate(today, qRate, dc);
    std::shared_ptr<SimpleQuote> rRate = std::make_shared<SimpleQuote>(0.0);
    std::shared_ptr<YieldTermStructure> rTS = flatRate(today, rRate, dc);
    std::vector<Volatility> vols(2);
    std::vector<Date> dates(2);

    for (Size i=0; i<LENGTH(values); i++) {
        Date exDate = today + Integer(values[i].t*365+0.5);
        Date intermDate = today + Integer(values[i].t1*365+0.5);
        std::shared_ptr<Exercise> exercise = std::make_shared<EuropeanExercise>(exDate);
        dates[0] = intermDate;
        dates[1] = exDate;

        spot ->setValue(values[i].s);
        qRate->setValue(values[i].q);
        rRate->setValue(values[i].r);
        vols[0] = values[i].v1;
        vols[1] = values[i].v;

        std::shared_ptr<BlackVolTermStructure> volTS =
                        std::make_shared<BlackVarianceCurve>(today, dates, vols, dc, true);

        std::shared_ptr<GeneralizedBlackScholesProcess> stochProcess =
                    std::make_shared<BlackScholesMertonProcess>(
                                       Handle<Quote>(spot),
                                       Handle<YieldTermStructure>(qTS),
                                       Handle<YieldTermStructure>(rTS),
                                       Handle<BlackVolTermStructure>(volTS));

        std::shared_ptr<PricingEngine> engine;
        engine =
            MakeMCVarianceSwapEngine<PseudoRandom>(stochProcess)
            .withStepsPerYear(250)
            .withSamples(1023)
            .withSeed(42);

        VarianceSwap varianceSwap(values[i].type,
                                  values[i].varStrike,
                                  values[i].nominal,
                                  today,
                                  exDate);
        varianceSwap.setPricingEngine(engine);

        Real calculated = varianceSwap.variance();
        Real expected = values[i].result;
        Real error = std::fabs(calculated-expected);
        if (error>values[i].tol)
            REPORT_FAILURE("value", values[i].type, values[i].varStrike,
                           values[i].nominal, values[i].s, values[i].q,
                           values[i].r, today, exDate, values[i].v, expected,
                           calculated, error, values[i].tol);
    }
}
