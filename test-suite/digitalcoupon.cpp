/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2007 Cristina Duminuco
 Copyright (C) 2007 Giorgio Facchinetti

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
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/cashflows/digitalcoupon.hpp>
#include <ql/cashflows/capflooredcoupon.hpp>
#include <ql/settings.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/optionlet/constantoptionletvol.hpp>
#include <ql/pricingengines/blackformula.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/pricingengines/vanilla/analyticeuropeanengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/math/distributions/normaldistribution.hpp>

using namespace QuantLib;


namespace {

    struct CommonVars {
        // global data
        Date today, settlement;
        Real nominal;
        Calendar calendar;
        std::shared_ptr<IborIndex> index;
        Natural fixingDays;
        RelinkableHandle<YieldTermStructure> termStructure;
        Real optionTolerance;
        Real blackTolerance;

        // cleanup
        SavedSettings backup;

        // setup
        CommonVars() {
            fixingDays = 2;
            nominal = 1000000.0;
            index = std::make_shared<Euribor6M>(termStructure);
            calendar = index->fixingCalendar();
            today = calendar.adjust(Settings::instance().evaluationDate());
            Settings::instance().evaluationDate() = today;
            settlement = calendar.advance(today,fixingDays,Days);
            termStructure.linkTo(flatRate(settlement,0.05,Actual365Fixed()));
            optionTolerance = 1.e-04;
            blackTolerance = 1e-10;
        }
    };

}


TEST_CASE("DigitalCoupon_AssetOrNothing", "[DigitalCoupon]") {

    INFO("Testing European asset-or-nothing digital coupon...");

    /*  Call Payoff = (aL+b)Heaviside(aL+b-X) =  a Max[L-X'] + (b+aX')Heaviside(L-X')
        Value Call = aF N(d1') + bN(d2')
        Put Payoff =  (aL+b)Heaviside(X-aL-b) = -a Max[X-L'] + (b+aX')Heaviside(X'-L)
        Value Put = aF N(-d1') + bN(-d2')
        where:
        d1' = ln(F/X')/stdDev + 0.5*stdDev;
    */

    CommonVars vars;

    Volatility vols[] = { 0.05, 0.15, 0.30 };
    Rate strikes[] = { 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07 };
    Real gearings[] = { 1.0, 2.8 };
    Rate spreads[] = { 0.0, 0.005 };

    Real gap = 1e-7; /* low, in order to compare digital option value
                        with black formula result */
    std::shared_ptr<DigitalReplication> replication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);
    for (Size i = 0; i< LENGTH(vols); i++) {
            Volatility capletVol = vols[i];
            RelinkableHandle<OptionletVolatilityStructure> vol;
            vol.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today,
                                            vars.calendar, Following,
                                            capletVol, Actual360()));
        for (Size j=0; j<LENGTH(strikes); j++) {
            Rate strike = strikes[j];
            for (Size k=9; k<10; k++) {
                Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
                Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
                Rate nullstrike = Null<Rate>();
                Date paymentDate = endDate;
                for (Size h=0; h<LENGTH(gearings); h++) {

                    Real gearing = gearings[h];
                    Rate spread = spreads[h];

                    std::shared_ptr<FloatingRateCoupon> underlying =
                        std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                                   startDate, endDate,
                                   vars.fixingDays, vars.index,
                                   gearing, spread);
                    // Floating Rate Coupon - Call Digital option
                    DigitalCoupon digitalCappedCoupon(underlying,
                                        strike, Position::Short, false, nullstrike,
                                        nullstrike, Position::Short, false, nullstrike,
                                        replication);
                    std::shared_ptr<IborCouponPricer> pricer =
                                                            std::make_shared<BlackIborCouponPricer>(vol);
                    digitalCappedCoupon.setPricer(pricer);

                    // Check digital option price vs N(d1) price
                    Time accrualPeriod = underlying->accrualPeriod();
                    Real discount = vars.termStructure->discount(endDate);
                    Date exerciseDate = underlying->fixingDate();
                    Rate forward = underlying->rate();
                    Rate effFwd = (forward-spread)/gearing;
                    Rate effStrike = (strike-spread)/gearing;
                    Real stdDev = std::sqrt(vol->blackVariance(exerciseDate, effStrike));
                    CumulativeNormalDistribution phi;
                    Real d1 = std::log(effFwd/effStrike)/stdDev + 0.5*stdDev;
                    Real d2 = d1 - stdDev;
                    Real N_d1 = phi(d1);
                    Real N_d2 = phi(d2);
                    Real nd1Price = (gearing * effFwd * N_d1 + spread * N_d2)
                                  * vars.nominal * accrualPeriod * discount;
                    Real optionPrice = digitalCappedCoupon.callOptionRate() *
                                       vars.nominal * accrualPeriod * discount;
                    Real error = std::abs(nd1Price - optionPrice);
                    if (error>vars.optionTolerance)
                        FAIL_CHECK("\nDigital Call Option:" <<
                            "\nVolatility = " << io::rate(capletVol) <<
                            "\nStrike = " << io::rate(strike) <<
                            "\nExercise = " << k+1 << " years" <<
                            "\nOption price by replication = "  << optionPrice <<
                            "\nOption price by Cox-Rubinstein formula = " << nd1Price <<
                            "\nError " << error);

                    // Check digital option price vs N(d1) price using Vanilla Option class
                    if (spread==0.0) {
                        std::shared_ptr<Exercise> exercise =
                            std::make_shared<EuropeanExercise>(exerciseDate);
                        Real discountAtFixing = vars.termStructure->discount(exerciseDate);
                        std::shared_ptr<SimpleQuote> fwd =
                            std::make_shared<SimpleQuote>(effFwd*discountAtFixing);
                        std::shared_ptr<SimpleQuote> qRate = std::make_shared<SimpleQuote>(0.0);
                        std::shared_ptr<YieldTermStructure> qTS
                             = flatRate(vars.today, qRate, Actual360());
                        std::shared_ptr<BlackVolTermStructure> volTS
                             = flatVol(vars.today, capletVol, Actual360());
                        std::shared_ptr<StrikedTypePayoff> callPayoff =
                            std::make_shared<AssetOrNothingPayoff>(Option::Call,effStrike);
                        std::shared_ptr<BlackScholesMertonProcess> stochProcess =
                            std::make_shared<BlackScholesMertonProcess>(Handle<Quote>(fwd),
                                              Handle<YieldTermStructure>(qTS),
                                              Handle<YieldTermStructure>(vars.termStructure),
                                              Handle<BlackVolTermStructure>(volTS));
                        std::shared_ptr<PricingEngine> engine =
                            std::make_shared<AnalyticEuropeanEngine>(stochProcess);
                        VanillaOption callOpt(callPayoff, exercise);
                        callOpt.setPricingEngine(engine);
                        Real callVO = vars.nominal * gearing
                                               * accrualPeriod * callOpt.NPV()
                                               * discount / discountAtFixing
                                               * forward / effFwd;
                        error = std::abs(nd1Price - callVO);
                        if (error>vars.blackTolerance)
                            FAIL_CHECK("\nDigital Call Option:" <<
                            "\nVolatility = " << io::rate(capletVol) <<
                            "\nStrike = " << io::rate(strike) <<
                            "\nExercise = " << k+1 << " years" <<
                            "\nOption price by Black asset-ot-nothing payoff = " << callVO <<
                            "\nOption price by Cox-Rubinstein = " << nd1Price <<
                            "\nError " << error );
                    }

                    // Floating Rate Coupon + Put Digital option
                    DigitalCoupon digitalFlooredCoupon(underlying,
                                        nullstrike, Position::Long, false, nullstrike,
                                        strike, Position::Long, false, nullstrike,
                                        replication);
                    digitalFlooredCoupon.setPricer(pricer);

                    // Check digital option price vs N(d1) price
                    N_d1 = phi(-d1);
                    N_d2 = phi(-d2);
                    nd1Price = (gearing * effFwd * N_d1 + spread * N_d2)
                             * vars.nominal * accrualPeriod * discount;
                    optionPrice = digitalFlooredCoupon.putOptionRate() *
                                  vars.nominal * accrualPeriod * discount;
                    error = std::abs(nd1Price - optionPrice);
                    if (error>vars.optionTolerance)
                        FAIL_CHECK("\nDigital Put Option:" <<
                                    "\nVolatility = " << io::rate(capletVol) <<
                                    "\nStrike = " << io::rate(strike) <<
                                    "\nExercise = " << k+1 << " years" <<
                                    "\nOption price by replication = "  << optionPrice <<
                                    "\nOption price by Cox-Rubinstein = " << nd1Price <<
                                    "\nError " << error );

                    // Check digital option price vs N(d1) price using Vanilla Option class
                    if (spread==0.0) {
                        std::shared_ptr<Exercise> exercise =
                            std::make_shared<EuropeanExercise>(exerciseDate);
                        Real discountAtFixing = vars.termStructure->discount(exerciseDate);
                        std::shared_ptr<SimpleQuote> fwd =
                            std::make_shared<SimpleQuote>(effFwd*discountAtFixing);
                        std::shared_ptr<SimpleQuote> qRate = std::make_shared<SimpleQuote>(0.0);
                        std::shared_ptr<YieldTermStructure> qTS
                             = flatRate(vars.today, qRate, Actual360());
                        std::shared_ptr<BlackVolTermStructure> volTS
                             = flatVol(vars.today, capletVol, Actual360());
                        std::shared_ptr<BlackScholesMertonProcess> stochProcess =
                            std::make_shared<BlackScholesMertonProcess>(Handle<Quote>(fwd),
                                              Handle<YieldTermStructure>(qTS),
                                              Handle<YieldTermStructure>(vars.termStructure),
                                              Handle<BlackVolTermStructure>(volTS));
                        std::shared_ptr<StrikedTypePayoff> putPayoff =
                            std::make_shared<AssetOrNothingPayoff>(Option::Put, effStrike);
                        std::shared_ptr<PricingEngine> engine = std::make_shared<AnalyticEuropeanEngine>(stochProcess);
                        VanillaOption putOpt(putPayoff, exercise);
                        putOpt.setPricingEngine(engine);
                        Real putVO  = vars.nominal * gearing
                                               * accrualPeriod * putOpt.NPV()
                                               * discount / discountAtFixing
                                               * forward / effFwd;
                        error = std::abs(nd1Price - putVO);
                        if (error>vars.blackTolerance)
                            FAIL_CHECK("\nDigital Put Option:" <<
                            "\nVolatility = " << io::rate(capletVol) <<
                            "\nStrike = " << io::rate(strike) <<
                            "\nExercise = " << k+1 << " years" <<
                            "\nOption price by Black asset-ot-nothing payoff = " << putVO <<
                            "\nOption price by Cox-Rubinstein = " << nd1Price <<
                            "\nError " << error );
                    }
                }
            }
        }
    }
}

TEST_CASE("DigitalCoupon_AssetOrNothingDeepInTheMoney", "[DigitalCoupon]") {

    INFO("Testing European deep in-the-money asset-or-nothing "
                       "digital coupon...");

    CommonVars vars;

    Real gearing = 1.0;
    Real spread = 0.0;

    Volatility capletVolatility = 0.0001;
    RelinkableHandle<OptionletVolatilityStructure> volatility;
    volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                    capletVolatility, Actual360()));
    Real gap = 1e-4;
    std::shared_ptr<DigitalReplication> replication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);

    for (Size k = 0; k<10; k++) {   // Loop on start and end dates
        Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
        Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
        Rate nullstrike = Null<Rate>();
        Date paymentDate = endDate;

        std::shared_ptr<FloatingRateCoupon> underlying =
            std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                       startDate, endDate,
                       vars.fixingDays, vars.index,
                       gearing, spread);

        // Floating Rate Coupon - Deep-in-the-money Call Digital option
        Rate strike = 0.001;
        DigitalCoupon digitalCappedCoupon(underlying,
                                          strike, Position::Short, false, nullstrike,
                                          nullstrike, Position::Short, false, nullstrike,
                                          replication);
        std::shared_ptr<IborCouponPricer> pricer =
            std::make_shared<BlackIborCouponPricer>(volatility);
        digitalCappedCoupon.setPricer(pricer);

        // Check price vs its target price
        Time accrualPeriod = underlying->accrualPeriod();
        Real discount = vars.termStructure->discount(endDate);

        Real targetOptionPrice = underlying->price(vars.termStructure);
        Real targetPrice = 0.0;
        Real digitalPrice = digitalCappedCoupon.price(vars.termStructure);
        Real error = std::fabs(targetPrice - digitalPrice);
        Real tolerance = 1e-08;
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon - Digital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon Price = "  << digitalPrice <<
                        "\nTarget price = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option price
        Real replicationOptionPrice = digitalCappedCoupon.callOptionRate() *
                                      vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        Real optionTolerance = 1e-08;
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = " << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error);

        // Floating Rate Coupon + Deep-in-the-money Put Digital option
        strike = 0.99;
        DigitalCoupon digitalFlooredCoupon(underlying,
                                           nullstrike, Position::Long, false, nullstrike,
                                           strike, Position::Long, false, nullstrike,
                                           replication);
        digitalFlooredCoupon.setPricer(pricer);

        // Check price vs its target price
        targetOptionPrice = underlying->price(vars.termStructure);
        targetPrice = underlying->price(vars.termStructure) + targetOptionPrice ;
        digitalPrice = digitalFlooredCoupon.price(vars.termStructure);
        error = std::fabs(targetPrice - digitalPrice);
        tolerance = 2.5e-06;
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon + Digital Put Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nDigital coupon price = "  << digitalPrice <<
                        "\nTarget price = " << targetPrice <<
                        "\nError " << error);

        // Check digital option
        replicationOptionPrice = digitalFlooredCoupon.putOptionRate() *
                                 vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        optionTolerance = 2.5e-06;
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Put Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = " << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError " << error);
    }
}

TEST_CASE("DigitalCoupon_AssetOrNothingDeepOutTheMoney", "[DigitalCoupon]") {

    INFO("Testing European deep out-the-money asset-or-nothing "
                       "digital coupon...");

    CommonVars vars;

    Real gearing = 1.0;
    Real spread = 0.0;

    Volatility capletVolatility = 0.0001;
    RelinkableHandle<OptionletVolatilityStructure> volatility;
    volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                    capletVolatility, Actual360()));
    Real gap = 1e-4;
    std::shared_ptr<DigitalReplication> replication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);

    for (Size k = 0; k<10; k++) { // loop on start and end dates
        Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
        Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
        Rate nullstrike = Null<Rate>();
        Date paymentDate = endDate;

        std::shared_ptr<FloatingRateCoupon> underlying =
            std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                       startDate, endDate,
                       vars.fixingDays, vars.index,
                       gearing, spread);

        // Floating Rate Coupon - Deep-out-of-the-money Call Digital option
        Rate strike = 0.99;
        DigitalCoupon digitalCappedCoupon(underlying,
                                          strike, Position::Short, false, nullstrike,
                                          nullstrike, Position::Long, false, nullstrike,
                                          replication/*Replication::Central, gap*/);
        std::shared_ptr<IborCouponPricer> pricer = std::make_shared<BlackIborCouponPricer>(volatility);
        digitalCappedCoupon.setPricer(pricer);

        // Check price vs its target
        Time accrualPeriod = underlying->accrualPeriod();
        Real discount = vars.termStructure->discount(endDate);

        Real targetPrice = underlying->price(vars.termStructure);
        Real digitalPrice = digitalCappedCoupon.price(vars.termStructure);
        Real error = std::fabs(targetPrice - digitalPrice);
        Real tolerance = 1e-10;
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon - Digital Call Option :" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option price
        Real targetOptionPrice = 0.;
        Real replicationOptionPrice = digitalCappedCoupon.callOptionRate() *
                                      vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        Real optionTolerance = 1e-08;
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = "  << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error );

        // Floating Rate Coupon - Deep-out-of-the-money Put Digital option
        strike = 0.01;
        DigitalCoupon digitalFlooredCoupon(underlying,
                                           nullstrike, Position::Long, false, nullstrike,
                                           strike, Position::Long, false, nullstrike,
                                           replication);
        digitalFlooredCoupon.setPricer(pricer);

        // Check price vs its target
        targetPrice = underlying->price(vars.termStructure);
        digitalPrice = digitalFlooredCoupon.price(vars.termStructure);
        tolerance = 1e-08;
        error = std::fabs(targetPrice - digitalPrice);
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon + Digital Put Coupon:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option
        targetOptionPrice = 0.0;
        replicationOptionPrice = digitalFlooredCoupon.putOptionRate() *
                                 vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Put Coupon:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = " << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error );
    }
}

TEST_CASE("DigitalCoupon_CashOrNothing", "[DigitalCoupon]") {

    INFO("Testing European cash-or-nothing digital coupon...");

    /*  Call Payoff = R Heaviside(aL+b-X)
        Value Call = R N(d2')
        Put Payoff =  R Heaviside(X-aL-b)
        Value Put = R N(-d2')
        where:
        d2' = ln(F/X')/stdDev - 0.5*stdDev;
    */

    CommonVars vars;

    Volatility vols[] = { 0.05, 0.15, 0.30 };
    Rate strikes[] = { 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07 };

    Real gearing = 3.0;
    Real spread = -0.0002;

    Real gap = 1e-08; /* very low, in order to compare digital option value
                                     with black formula result */
    std::shared_ptr<DigitalReplication> replication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);

    for (Size i = 0; i< LENGTH(vols); i++) {
            Volatility capletVol = vols[i];
            RelinkableHandle<OptionletVolatilityStructure> vol;
            vol.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today,
                                            vars.calendar, Following,
                                            capletVol, Actual360()));
        for (Size j = 0; j< LENGTH(strikes); j++) {
            Rate strike = strikes[j];
            for (Size k = 0; k<10; k++) {
                Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
                Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
                Rate nullstrike = Null<Rate>();
                Rate cashRate = 0.01;

                Date paymentDate = endDate;
                std::shared_ptr<FloatingRateCoupon> underlying =
                    std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                               startDate, endDate,
                               vars.fixingDays, vars.index,
                               gearing, spread);
                // Floating Rate Coupon - Call Digital option
                DigitalCoupon digitalCappedCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Short, false, nullstrike,
                                          replication);
                std::shared_ptr<IborCouponPricer> pricer = std::make_shared<BlackIborCouponPricer>(vol);
                digitalCappedCoupon.setPricer(pricer);

                // Check digital option price vs N(d2) price
                Date exerciseDate = underlying->fixingDate();
                Rate forward = underlying->rate();
                Rate effFwd = (forward-spread)/gearing;
                Rate effStrike = (strike-spread)/gearing;
                Time accrualPeriod = underlying->accrualPeriod();
                Real discount = vars.termStructure->discount(endDate);
                Real stdDev = std::sqrt(vol->blackVariance(exerciseDate, effStrike));
                Real ITM = blackFormulaCashItmProbability(Option::Call, effStrike,
                                                          effFwd, stdDev);
                Real nd2Price = ITM * vars.nominal * accrualPeriod * discount * cashRate;
                Real optionPrice = digitalCappedCoupon.callOptionRate() *
                                   vars.nominal * accrualPeriod * discount;
                Real error = std::abs(nd2Price - optionPrice);
                if (error>vars.optionTolerance)
                    FAIL_CHECK("\nDigital Call Option:" <<
                                "\nVolatility = " << io::rate(capletVol) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                "\nPrice by replication = " << optionPrice <<
                                "\nPrice by Reiner-Rubinstein = " << nd2Price <<
                                "\nError = " << error );

                // Check digital option price vs N(d2) price using Vanilla Option class
                std::shared_ptr<Exercise> exercise = std::make_shared<EuropeanExercise>(exerciseDate);
                Real discountAtFixing = vars.termStructure->discount(exerciseDate);
                std::shared_ptr<SimpleQuote> fwd = std::make_shared<SimpleQuote>(effFwd*discountAtFixing);
                std::shared_ptr<SimpleQuote> qRate = std::make_shared<SimpleQuote>(0.0);
                std::shared_ptr<YieldTermStructure> qTS = flatRate(vars.today, qRate, Actual360());
                std::shared_ptr<BlackVolTermStructure> volTS = flatVol(vars.today, capletVol,
                                                                         Actual360());
                std::shared_ptr<StrikedTypePayoff> callPayoff =
                                                        std::make_shared<CashOrNothingPayoff>(Option::Call, effStrike, cashRate);
                std::shared_ptr<BlackScholesMertonProcess> stochProcess =
                std::make_shared<BlackScholesMertonProcess>(Handle<Quote>(fwd),
                                          Handle<YieldTermStructure>(qTS),
                                          Handle<YieldTermStructure>(vars.termStructure),
                                          Handle<BlackVolTermStructure>(volTS));
                std::shared_ptr<PricingEngine> engine = std::make_shared<AnalyticEuropeanEngine>(stochProcess);
                VanillaOption callOpt(callPayoff, exercise);
                callOpt.setPricingEngine(engine);
                Real callVO = vars.nominal * accrualPeriod * callOpt.NPV()
                                       * discount / discountAtFixing;
                error = std::abs(nd2Price - callVO);
                if (error>vars.blackTolerance)
                    FAIL_CHECK("\nDigital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVol) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nOption price by Black asset-ot-nothing payoff = " << callVO <<
                        "\nOption price by Reiner-Rubinstein = " << nd2Price <<
                        "\nError " << error );

                // Floating Rate Coupon + Put Digital option
                DigitalCoupon digitalFlooredCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Long, false, cashRate,
                                          replication);
                digitalFlooredCoupon.setPricer(pricer);


                // Check digital option price vs N(d2) price
                ITM = blackFormulaCashItmProbability(Option::Put,
                                                     effStrike,
                                                     effFwd,
                                                     stdDev);
                nd2Price = ITM * vars.nominal * accrualPeriod * discount * cashRate;
                optionPrice = digitalFlooredCoupon.putOptionRate() *
                              vars.nominal * accrualPeriod * discount;
                error = std::abs(nd2Price - optionPrice);
                if (error>vars.optionTolerance)
                    FAIL_CHECK("\nPut Digital Option:" <<
                                "\nVolatility = " << io::rate(capletVol) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                "\nPrice by replication = "  << optionPrice <<
                                "\nPrice by Reiner-Rubinstein = " << nd2Price <<
                                "\nError = " << error );

                // Check digital option price vs N(d2) price using Vanilla Option class
                std::shared_ptr<StrikedTypePayoff> putPayoff =
                    std::make_shared<CashOrNothingPayoff>(Option::Put, effStrike, cashRate);
                VanillaOption putOpt(putPayoff, exercise);
                putOpt.setPricingEngine(engine);
                Real putVO  = vars.nominal * accrualPeriod * putOpt.NPV()
                                       * discount / discountAtFixing;
                error = std::abs(nd2Price - putVO);
                if (error>vars.blackTolerance)
                    FAIL_CHECK("\nDigital Put Option:" <<
                        "\nVolatility = " << io::rate(capletVol) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nOption price by Black asset-ot-nothing payoff = "  << putVO <<
                        "\nOption price by Reiner-Rubinstein = " << nd2Price <<
                        "\nError " << error );
            }
        }
    }
}

TEST_CASE("DigitalCoupon_CashOrNothingDeepInTheMoney", "[DigitalCoupon]") {

    INFO("Testing European deep in-the-money cash-or-nothing "
                       "digital coupon...");

    CommonVars vars;

    Real gearing = 1.0;
    Real spread = 0.0;

    Volatility capletVolatility = 0.0001;
    RelinkableHandle<OptionletVolatilityStructure> volatility;
    volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                    capletVolatility, Actual360()));

    for (Size k = 0; k<10; k++) {   // Loop on start and end dates
        Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
        Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
        Rate nullstrike = Null<Rate>();
        Rate cashRate = 0.01;
        Real gap = 1e-4;
        std::shared_ptr<DigitalReplication> replication =
            std::make_shared<DigitalReplication>(Replication::Central, gap);
        Date paymentDate = endDate;

        std::shared_ptr<FloatingRateCoupon> underlying =
            std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                       startDate, endDate,
                       vars.fixingDays, vars.index,
                       gearing, spread);
        // Floating Rate Coupon - Deep-in-the-money Call Digital option
        Rate strike = 0.001;
        DigitalCoupon digitalCappedCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Short, false, nullstrike,
                                          replication);
        std::shared_ptr<IborCouponPricer> pricer =
            std::make_shared<BlackIborCouponPricer>(volatility);
        digitalCappedCoupon.setPricer(pricer);

        // Check price vs its target
        Time accrualPeriod = underlying->accrualPeriod();
        Real discount = vars.termStructure->discount(endDate);

        Real targetOptionPrice = cashRate * vars.nominal * accrualPeriod * discount;
        Real targetPrice = underlying->price(vars.termStructure) - targetOptionPrice;
        Real digitalPrice = digitalCappedCoupon.price(vars.termStructure);

        Real error = std::fabs(targetPrice - digitalPrice);
        Real tolerance = 1e-07;
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon - Digital Call Coupon:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price = " << targetPrice <<
                        "\nError " << error );

        // Check digital option price
        Real replicationOptionPrice = digitalCappedCoupon.callOptionRate() *
                                      vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        Real optionTolerance = 1e-07;
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = " << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error);

        // Floating Rate Coupon + Deep-in-the-money Put Digital option
        strike = 0.99;
        DigitalCoupon digitalFlooredCoupon(underlying,
                                           nullstrike, Position::Long, false, nullstrike,
                                           strike, Position::Long, false, cashRate,
                                           replication);
        digitalFlooredCoupon.setPricer(pricer);

        // Check price vs its target
        targetPrice = underlying->price(vars.termStructure) + targetOptionPrice;
        digitalPrice = digitalFlooredCoupon.price(vars.termStructure);
        error = std::fabs(targetPrice - digitalPrice);
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon + Digital Put Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price  = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option
        replicationOptionPrice = digitalFlooredCoupon.putOptionRate() *
                                 vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Put Coupon:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = " << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error );
    }
}

TEST_CASE("DigitalCoupon_CashOrNothingDeepOutTheMoney", "[DigitalCoupon]") {

    INFO("Testing European deep out-the-money cash-or-nothing "
                       "digital coupon...");

    CommonVars vars;

    Real gearing = 1.0;
    Real spread = 0.0;

    Volatility capletVolatility = 0.0001;
    RelinkableHandle<OptionletVolatilityStructure> volatility;
    volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                    capletVolatility, Actual360()));

    for (Size k = 0; k<10; k++) { // loop on start and end dates
        Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
        Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
        Rate nullstrike = Null<Rate>();
        Rate cashRate = 0.01;
        Real gap = 1e-4;
        std::shared_ptr<DigitalReplication> replication =
            std::make_shared<DigitalReplication>(Replication::Central, gap);
        Date paymentDate = endDate;

        std::shared_ptr<FloatingRateCoupon> underlying =
            std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                       startDate, endDate,
                       vars.fixingDays, vars.index,
                       gearing, spread);
        // Deep out-of-the-money Capped Digital Coupon
        Rate strike = 0.99;
        DigitalCoupon digitalCappedCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Short, false, nullstrike,
                                          replication);

        std::shared_ptr<IborCouponPricer> pricer = std::make_shared<BlackIborCouponPricer>(volatility);
        digitalCappedCoupon.setPricer(pricer);

        // Check price vs its target
        Time accrualPeriod = underlying->accrualPeriod();
        Real discount = vars.termStructure->discount(endDate);

        Real targetPrice = underlying->price(vars.termStructure);
        Real digitalPrice = digitalCappedCoupon.price(vars.termStructure);
        Real error = std::fabs(targetPrice - digitalPrice);
        Real tolerance = 1e-10;
        if (error>tolerance)
            FAIL_CHECK("\nFloating Coupon + Digital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price  = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option price
        Real targetOptionPrice = 0.;
        Real replicationOptionPrice = digitalCappedCoupon.callOptionRate() *
                                      vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        Real optionTolerance = 1e-10;
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Call Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication = "  << replicationOptionPrice <<
                        "\nTarget price = " << targetOptionPrice <<
                        "\nError = " << error );

        // Deep out-of-the-money Floored Digital Coupon
        strike = 0.01;
        DigitalCoupon digitalFlooredCoupon(underlying,
                                           nullstrike, Position::Long, false, nullstrike,
                                           strike, Position::Long, false, cashRate,
                                           replication);
        digitalFlooredCoupon.setPricer(pricer);

        // Check price vs its target
        targetPrice = underlying->price(vars.termStructure);
        digitalPrice = digitalFlooredCoupon.price(vars.termStructure);
        tolerance = 1e-09;
        error = std::fabs(targetPrice - digitalPrice);
        if (error>tolerance)
            FAIL_CHECK("\nDigital Floored Coupon:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nCoupon price = "  << digitalPrice <<
                        "\nTarget price  = " << targetPrice <<
                        "\nError = " << error );

        // Check digital option
        targetOptionPrice = 0.0;
        replicationOptionPrice = digitalFlooredCoupon.putOptionRate() *
                                 vars.nominal * accrualPeriod * discount;
        error = std::abs(targetOptionPrice - replicationOptionPrice);
        if (error>optionTolerance)
            FAIL_CHECK("\nDigital Put Option:" <<
                        "\nVolatility = " << io::rate(capletVolatility) <<
                        "\nStrike = " << io::rate(strike) <<
                        "\nExercise = " << k+1 << " years" <<
                        "\nPrice by replication " << replicationOptionPrice <<
                        "\nTarget price " << targetOptionPrice <<
                        "\nError " << error );
    }
}


TEST_CASE("DigitalCoupon_CallPutParity", "[DigitalCoupon]") {

    INFO("Testing call/put parity for European digital coupon...");

    CommonVars vars;

    Volatility vols[] = { 0.05, 0.15, 0.30 };
    Rate strikes[] = { 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07 };

    Real gearing = 1.0;
    Real spread = 0.0;

    Real gap = 1e-04;
    std::shared_ptr<DigitalReplication> replication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);

    for (Size i = 0; i< LENGTH(vols); i++) {
            Volatility capletVolatility = vols[i];
            RelinkableHandle<OptionletVolatilityStructure> volatility;
            volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                            capletVolatility, Actual360()));
        for (Size j = 0; j< LENGTH(strikes); j++) {
            Rate strike = strikes[j];
            for (Size k = 0; k<10; k++) {
                Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
                Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
                Rate nullstrike = Null<Rate>();

                Date paymentDate = endDate;

                std::shared_ptr<FloatingRateCoupon> underlying =
                    std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                               startDate, endDate,
                               vars.fixingDays, vars.index,
                               gearing, spread);
                // Cash-or-Nothing
                Rate cashRate = 0.01;
                // Floating Rate Coupon + Call Digital option
                DigitalCoupon cash_digitalCallCoupon(underlying,
                                          strike, Position::Long, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          replication);
                std::shared_ptr<IborCouponPricer> pricer =
                    std::make_shared<BlackIborCouponPricer>(volatility);
                cash_digitalCallCoupon.setPricer(pricer);
                // Floating Rate Coupon - Put Digital option
                DigitalCoupon cash_digitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Short, false, cashRate,
                                          replication);

                cash_digitalPutCoupon.setPricer(pricer);
                Real digitalPrice = cash_digitalCallCoupon.price(vars.termStructure) -
                                    cash_digitalPutCoupon.price(vars.termStructure);
                // Target price
                Time accrualPeriod = underlying->accrualPeriod();
                Real discount = vars.termStructure->discount(endDate);
                Real targetPrice = vars.nominal * accrualPeriod *  discount * cashRate;

                Real error = std::fabs(targetPrice - digitalPrice);
                Real tolerance = 1.e-08;
                if (error>tolerance)
                    FAIL_CHECK("\nCash-or-nothing:" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                "\nPrice = "  << digitalPrice <<
                                "\nTarget Price  = " << targetPrice <<
                                "\nError = " << error );

                // Asset-or-Nothing
                // Floating Rate Coupon + Call Digital option
                DigitalCoupon asset_digitalCallCoupon(underlying,
                                          strike, Position::Long, false, nullstrike,
                                          nullstrike, Position::Long, false, nullstrike,
                                          replication);
                asset_digitalCallCoupon.setPricer(pricer);
                // Floating Rate Coupon - Put Digital option
                DigitalCoupon asset_digitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Short, false, nullstrike,
                                          replication);
                asset_digitalPutCoupon.setPricer(pricer);
                digitalPrice = asset_digitalCallCoupon.price(vars.termStructure) -
                               asset_digitalPutCoupon.price(vars.termStructure);
                // Target price
                targetPrice = vars.nominal *  accrualPeriod *  discount * underlying->rate();
                error = std::fabs(targetPrice - digitalPrice);
                tolerance = 1.e-07;
                if (error>tolerance)
                    FAIL_CHECK("\nAsset-or-nothing:" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                "\nPrice = "  << digitalPrice <<
                                "\nTarget Price  = " << targetPrice <<
                                "\nError = " << error );
            }
        }
    }
}

TEST_CASE("DigitalCoupon_ReplicationType", "[DigitalCoupon]") {

    INFO("Testing replication type for European digital coupon...");

    CommonVars vars;

    Volatility vols[] = { 0.05, 0.15, 0.30 };
    Rate strikes[] = { 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07 };

    Real gearing = 1.0;
    Real spread = 0.0;

    Real gap = 1e-04;
    std::shared_ptr<DigitalReplication> subReplication =
        std::make_shared<DigitalReplication>(Replication::Sub, gap);
    std::shared_ptr<DigitalReplication> centralReplication =
        std::make_shared<DigitalReplication>(Replication::Central, gap);
    std::shared_ptr<DigitalReplication> superReplication =
        std::make_shared<DigitalReplication>(Replication::Super, gap);

    for (Size i = 0; i< LENGTH(vols); i++) {
        Volatility capletVolatility = vols[i];
        RelinkableHandle<OptionletVolatilityStructure> volatility;
        volatility.linkTo(std::make_shared<ConstantOptionletVolatility>(vars.today, vars.calendar, Following,
                                    capletVolatility, Actual360()));
        for (Size j = 0; j< LENGTH(strikes); j++) {
            Rate strike = strikes[j];
            for (Size k = 0; k<10; k++) {
                Date startDate = vars.calendar.advance(vars.settlement,(k+1)*Years);
                Date endDate = vars.calendar.advance(vars.settlement,(k+2)*Years);
                Rate nullstrike = Null<Rate>();

                Date paymentDate = endDate;

                std::shared_ptr<FloatingRateCoupon> underlying =
                    std::make_shared<IborCoupon>(paymentDate, vars.nominal,
                               startDate, endDate,
                               vars.fixingDays, vars.index,
                               gearing, spread);
                // Cash-or-Nothing
                Rate cashRate = 0.005;
                // Floating Rate Coupon + Call Digital option
                DigitalCoupon sub_cash_longDigitalCallCoupon(underlying,
                                          strike, Position::Long, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          subReplication);
                DigitalCoupon central_cash_longDigitalCallCoupon(underlying,
                                          strike, Position::Long, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          centralReplication);
                DigitalCoupon over_cash_longDigitalCallCoupon(underlying,
                                          strike, Position::Long, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          superReplication);
                std::shared_ptr<IborCouponPricer> pricer =
                    std::make_shared<BlackIborCouponPricer>(volatility);
                sub_cash_longDigitalCallCoupon.setPricer(pricer);
                central_cash_longDigitalCallCoupon.setPricer(pricer);
                over_cash_longDigitalCallCoupon.setPricer(pricer);
                Real sub_digitalPrice = sub_cash_longDigitalCallCoupon.price(vars.termStructure);
                Real central_digitalPrice = central_cash_longDigitalCallCoupon.price(vars.termStructure);
                Real over_digitalPrice = over_cash_longDigitalCallCoupon.price(vars.termStructure);
                Real tolerance = 1.e-09;
                if ( ( (sub_digitalPrice > central_digitalPrice) &&
                        std::abs(central_digitalPrice - sub_digitalPrice)>tolerance ) ||
                     ( (central_digitalPrice>over_digitalPrice)  &&
                        std::abs(central_digitalPrice - over_digitalPrice)>tolerance ) )  {
                    FAIL_CHECK("\nCash-or-nothing: Floating Rate Coupon + Call Digital option" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                std::setprecision(20) <<
                                "\nSub-Replication Price = "  << sub_digitalPrice <<
                                "\nCentral-Replication Price = "  << central_digitalPrice <<
                                "\nOver-Replication Price = "  << over_digitalPrice);
                }

                // Floating Rate Coupon - Call Digital option
                DigitalCoupon sub_cash_shortDigitalCallCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          subReplication);
                DigitalCoupon central_cash_shortDigitalCallCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          centralReplication);
                DigitalCoupon over_cash_shortDigitalCallCoupon(underlying,
                                          strike, Position::Short, false, cashRate,
                                          nullstrike, Position::Long, false, nullstrike,
                                          superReplication);
                sub_cash_shortDigitalCallCoupon.setPricer(pricer);
                central_cash_shortDigitalCallCoupon.setPricer(pricer);
                over_cash_shortDigitalCallCoupon.setPricer(pricer);
                sub_digitalPrice = sub_cash_shortDigitalCallCoupon.price(vars.termStructure);
                central_digitalPrice = central_cash_shortDigitalCallCoupon.price(vars.termStructure);
                over_digitalPrice = over_cash_shortDigitalCallCoupon.price(vars.termStructure);
                if ( ( (sub_digitalPrice > central_digitalPrice) &&
                        std::abs(central_digitalPrice - sub_digitalPrice)>tolerance ) ||
                     ( (central_digitalPrice>over_digitalPrice)  &&
                        std::abs(central_digitalPrice - over_digitalPrice)>tolerance ) )
                    FAIL_CHECK("\nCash-or-nothing: Floating Rate Coupon - Call Digital option" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                std::setprecision(20) <<
                                "\nSub-Replication Price = "  << sub_digitalPrice <<
                                "\nCentral-Replication Price = "  << central_digitalPrice <<
                                "\nOver-Replication Price = "  << over_digitalPrice);
                // Floating Rate Coupon + Put Digital option
                DigitalCoupon sub_cash_longDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Long, false, cashRate,
                                          subReplication);
                DigitalCoupon central_cash_longDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Long, false, cashRate,
                                          centralReplication);
                DigitalCoupon over_cash_longDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Long, false, cashRate,
                                          superReplication);
                sub_cash_longDigitalPutCoupon.setPricer(pricer);
                central_cash_longDigitalPutCoupon.setPricer(pricer);
                over_cash_longDigitalPutCoupon.setPricer(pricer);
                sub_digitalPrice = sub_cash_longDigitalPutCoupon.price(vars.termStructure);
                central_digitalPrice = central_cash_longDigitalPutCoupon.price(vars.termStructure);
                over_digitalPrice = over_cash_longDigitalPutCoupon.price(vars.termStructure);
                if ( ( (sub_digitalPrice > central_digitalPrice) &&
                        std::abs(central_digitalPrice - sub_digitalPrice)>tolerance ) ||
                     ( (central_digitalPrice>over_digitalPrice)  &&
                        std::abs(central_digitalPrice - over_digitalPrice)>tolerance ) )
                    FAIL_CHECK("\nCash-or-nothing: Floating Rate Coupon + Put Digital option" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                std::setprecision(20) <<
                                "\nSub-Replication Price = "  << sub_digitalPrice <<
                                "\nCentral-Replication Price = "  << central_digitalPrice <<
                                "\nOver-Replication Price = "  << over_digitalPrice);

                // Floating Rate Coupon - Put Digital option
                DigitalCoupon sub_cash_shortDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Short, false, cashRate,
                                          subReplication);
                DigitalCoupon central_cash_shortDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Short, false, cashRate,
                                          centralReplication);
                DigitalCoupon over_cash_shortDigitalPutCoupon(underlying,
                                          nullstrike, Position::Long, false, nullstrike,
                                          strike, Position::Short, false, cashRate,
                                          superReplication);
                sub_cash_shortDigitalPutCoupon.setPricer(pricer);
                central_cash_shortDigitalPutCoupon.setPricer(pricer);
                over_cash_shortDigitalPutCoupon.setPricer(pricer);
                sub_digitalPrice = sub_cash_shortDigitalPutCoupon.price(vars.termStructure);
                central_digitalPrice = central_cash_shortDigitalPutCoupon.price(vars.termStructure);
                over_digitalPrice = over_cash_shortDigitalPutCoupon.price(vars.termStructure);
                if ( ( (sub_digitalPrice > central_digitalPrice) &&
                        std::abs(central_digitalPrice - sub_digitalPrice)>tolerance ) ||
                     ( (central_digitalPrice>over_digitalPrice)  &&
                        std::abs(central_digitalPrice - over_digitalPrice)>tolerance ) )
                    FAIL_CHECK("\nCash-or-nothing: Floating Rate Coupon + Call Digital option" <<
                                "\nVolatility = " << io::rate(capletVolatility) <<
                                "\nStrike = " << io::rate(strike) <<
                                "\nExercise = " << k+1 << " years" <<
                                std::setprecision(20) <<
                                "\nSub-Replication Price = "  << sub_digitalPrice <<
                                "\nCentral-Replication Price = "  << central_digitalPrice <<
                                "\nOver-Replication Price = "  << over_digitalPrice);
            }
        }
    }
}
