/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*!
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl
 Copyright (C) 2003, 2004, 2005, 2006, 2007 StatPro Italia srl
 Copyright (C) 2004 Ferdinando Ametrano

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

/*  This example shows how to set up a Term Structure and then price a simple
    swap.
*/

#include <ql/qldefines.hpp>
#include <ql/termstructures/yield/piecewiseyieldcurve.hpp>
#include <ql/termstructures/yield/ratehelpers.hpp>
#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/time/imm.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/time/daycounters/actual360.hpp>
#include <ql/time/daycounters/thirty360.hpp>
#include <ql/time/daycounters/actualactual.hpp>

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace QuantLib;

#if defined(QL_ENABLE_SESSIONS)
namespace QuantLib {

    Integer sessionId() { return 0; }

}
#endif


int main(int, char *[]) {

    try {

        std::chrono::time_point<std::chrono::steady_clock> startT = std::chrono::steady_clock::now();
        std::cout << std::endl;

        /*********************
         ***  MARKET DATA  ***
         *********************/

        Calendar calendar = TARGET();
        Date settlementDate(22, September, 2004);
        // must be a business day
        settlementDate = calendar.adjust(settlementDate);

        Integer fixingDays = 2;
        Date todaysDate = calendar.advance(settlementDate, -fixingDays, Days);
        // nothing to do with Date::todaysDate
        Settings::instance().evaluationDate() = todaysDate;


        todaysDate = Settings::instance().evaluationDate();
        std::cout << "Today: " << todaysDate.weekday()
                  << ", " << todaysDate << std::endl;

        std::cout << "Settlement date: " << settlementDate.weekday()
                  << ", " << settlementDate << std::endl;

        // deposits
        Rate d1wQuote = 0.0382;
        Rate d1mQuote = 0.0372;
        Rate d3mQuote = 0.0363;
        Rate d6mQuote = 0.0353;
        Rate d9mQuote = 0.0348;
        Rate d1yQuote = 0.0345;
        // FRAs
        Rate fra3x6Quote = 0.037125;
        Rate fra6x9Quote = 0.037125;
        Rate fra6x12Quote = 0.037125;
        // futures
        Real fut1Quote = 96.2875;
        Real fut2Quote = 96.7875;
        Real fut3Quote = 96.9875;
        Real fut4Quote = 96.6875;
        Real fut5Quote = 96.4875;
        Real fut6Quote = 96.3875;
        Real fut7Quote = 96.2875;
        Real fut8Quote = 96.0875;
        // swaps
        Rate s2yQuote = 0.037125;
        Rate s3yQuote = 0.0398;
        Rate s5yQuote = 0.0443;
        Rate s10yQuote = 0.05165;
        Rate s15yQuote = 0.055175;


        /********************
         ***    QUOTES    ***
         ********************/

        // SimpleQuote stores a value which can be manually changed;
        // other Quote subclasses could read the value from a database
        // or some kind of data feed.

        // deposits
        std::shared_ptr < Quote > d1wRate = std::make_shared<SimpleQuote>(d1wQuote);
        std::shared_ptr < Quote > d1mRate = std::make_shared<SimpleQuote>(d1mQuote);
        std::shared_ptr < Quote > d3mRate = std::make_shared<SimpleQuote>(d3mQuote);
        std::shared_ptr < Quote > d6mRate = std::make_shared<SimpleQuote>(d6mQuote);
        std::shared_ptr < Quote > d9mRate = std::make_shared<SimpleQuote>(d9mQuote);
        std::shared_ptr < Quote > d1yRate = std::make_shared<SimpleQuote>(d1yQuote);
        // FRAs
        std::shared_ptr < Quote > fra3x6Rate = std::make_shared<SimpleQuote>(fra3x6Quote);
        std::shared_ptr < Quote > fra6x9Rate = std::make_shared<SimpleQuote>(fra6x9Quote);
        std::shared_ptr < Quote > fra6x12Rate = std::make_shared<SimpleQuote>(fra6x12Quote);
        // futures
        std::shared_ptr < Quote > fut1Price = std::make_shared<SimpleQuote>(fut1Quote);
        std::shared_ptr < Quote > fut2Price = std::make_shared<SimpleQuote>(fut2Quote);
        std::shared_ptr < Quote > fut3Price = std::make_shared<SimpleQuote>(fut3Quote);
        std::shared_ptr < Quote > fut4Price = std::make_shared<SimpleQuote>(fut4Quote);
        std::shared_ptr < Quote > fut5Price = std::make_shared<SimpleQuote>(fut5Quote);
        std::shared_ptr < Quote > fut6Price = std::make_shared<SimpleQuote>(fut6Quote);
        std::shared_ptr < Quote > fut7Price = std::make_shared<SimpleQuote>(fut7Quote);
        std::shared_ptr < Quote > fut8Price = std::make_shared<SimpleQuote>(fut8Quote);
        // swaps
        std::shared_ptr < Quote > s2yRate = std::make_shared<SimpleQuote>(s2yQuote);
        std::shared_ptr < Quote > s3yRate = std::make_shared<SimpleQuote>(s3yQuote);
        std::shared_ptr < Quote > s5yRate = std::make_shared<SimpleQuote>(s5yQuote);
        std::shared_ptr < Quote > s10yRate = std::make_shared<SimpleQuote>(s10yQuote);
        std::shared_ptr < Quote > s15yRate = std::make_shared<SimpleQuote>(s15yQuote);


        /*********************
         ***  RATE HELPERS ***
         *********************/

        // RateHelpers are built from the above quotes together with
        // other instrument dependant infos.  Quotes are passed in
        // relinkable handles which could be relinked to some other
        // data source later.

        // deposits
        DayCounter depositDayCounter = Actual360();

        std::shared_ptr < RateHelper > d1w = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d1wRate),
                 1 * Weeks, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > d1m = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d1mRate),
                 1 * Months, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > d3m = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d3mRate),
                 3 * Months, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > d6m = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d6mRate),
                 6 * Months, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > d9m = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d9mRate),
                 9 * Months, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > d1y = std::make_shared<DepositRateHelper>
                (Handle<Quote>(d1yRate),
                 1 * Years, fixingDays,
                 calendar, ModifiedFollowing,
                 true, depositDayCounter);


        // setup FRAs
        std::shared_ptr < RateHelper > fra3x6 = std::make_shared<FraRateHelper>
                (Handle<Quote>(fra3x6Rate),
                 3, 6, fixingDays, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > fra6x9 = std::make_shared<FraRateHelper>
                (Handle<Quote>(fra6x9Rate),
                 6, 9, fixingDays, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        std::shared_ptr < RateHelper > fra6x12 = std::make_shared<FraRateHelper>
                (Handle<Quote>(fra6x12Rate),
                 6, 12, fixingDays, calendar, ModifiedFollowing,
                 true, depositDayCounter);


        // setup futures
        // Rate convexityAdjustment = 0.0;
        Integer futMonths = 3;
        Date imm = IMM::nextDate(settlementDate);
        std::shared_ptr < RateHelper > fut1 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut1Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut2 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut2Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut3 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut3Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut4 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut4Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut5 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut5Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut6 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut6Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut7 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut7Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);
        imm = IMM::nextDate(imm + 1);
        std::shared_ptr < RateHelper > fut8 = std::make_shared<FuturesRateHelper>
                (Handle<Quote>(fut8Price),
                 imm,
                 futMonths, calendar, ModifiedFollowing,
                 true, depositDayCounter);


        // setup swaps
        Frequency swFixedLegFrequency = Annual;
        BusinessDayConvention swFixedLegConvention = Unadjusted;
        DayCounter swFixedLegDayCounter = Thirty360(Thirty360::European);
        std::shared_ptr < IborIndex > swFloatingLegIndex = std::make_shared<Euribor6M>();

        std::shared_ptr < RateHelper > s2y = std::make_shared<SwapRateHelper>
                (Handle<Quote>(s2yRate), 2 * Years,
                 calendar, swFixedLegFrequency,
                 swFixedLegConvention, swFixedLegDayCounter,
                 swFloatingLegIndex);
        std::shared_ptr < RateHelper > s3y = std::make_shared<SwapRateHelper>
                (Handle<Quote>(s3yRate), 3 * Years,
                 calendar, swFixedLegFrequency,
                 swFixedLegConvention, swFixedLegDayCounter,
                 swFloatingLegIndex);
        std::shared_ptr < RateHelper > s5y = std::make_shared<SwapRateHelper>
                (Handle<Quote>(s5yRate), 5 * Years,
                 calendar, swFixedLegFrequency,
                 swFixedLegConvention, swFixedLegDayCounter,
                 swFloatingLegIndex);
        std::shared_ptr < RateHelper > s10y = std::make_shared<SwapRateHelper>
                (Handle<Quote>(s10yRate), 10 * Years,
                 calendar, swFixedLegFrequency,
                 swFixedLegConvention, swFixedLegDayCounter,
                 swFloatingLegIndex);
        std::shared_ptr < RateHelper > s15y = std::make_shared<SwapRateHelper>
                (Handle<Quote>(s15yRate), 15 * Years,
                 calendar, swFixedLegFrequency,
                 swFixedLegConvention, swFixedLegDayCounter,
                 swFloatingLegIndex);


        /*********************
         **  CURVE BUILDING **
         *********************/

        // Any DayCounter would be fine.
        // ActualActual::ISDA ensures that 30 years is 30.0
        DayCounter termStructureDayCounter =
                ActualActual(ActualActual::ISDA);


        double tolerance = 1.0e-15;

        // A depo-swap curve
        std::vector<std::shared_ptr<RateHelper> > depoSwapInstruments;
        depoSwapInstruments.emplace_back(d1w);
        depoSwapInstruments.emplace_back(d1m);
        depoSwapInstruments.emplace_back(d3m);
        depoSwapInstruments.emplace_back(d6m);
        depoSwapInstruments.emplace_back(d9m);
        depoSwapInstruments.emplace_back(d1y);
        depoSwapInstruments.emplace_back(s2y);
        depoSwapInstruments.emplace_back(s3y);
        depoSwapInstruments.emplace_back(s5y);
        depoSwapInstruments.emplace_back(s10y);
        depoSwapInstruments.emplace_back(s15y);
        std::shared_ptr < YieldTermStructure > depoSwapTermStructure =
                std::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(
                        settlementDate, depoSwapInstruments,
                        termStructureDayCounter,
                        tolerance);


        // A depo-futures-swap curve
        std::vector<std::shared_ptr<RateHelper> > depoFutSwapInstruments;
        depoFutSwapInstruments.emplace_back(d1w);
        depoFutSwapInstruments.emplace_back(d1m);
        depoFutSwapInstruments.emplace_back(fut1);
        depoFutSwapInstruments.emplace_back(fut2);
        depoFutSwapInstruments.emplace_back(fut3);
        depoFutSwapInstruments.emplace_back(fut4);
        depoFutSwapInstruments.emplace_back(fut5);
        depoFutSwapInstruments.emplace_back(fut6);
        depoFutSwapInstruments.emplace_back(fut7);
        depoFutSwapInstruments.emplace_back(fut8);
        depoFutSwapInstruments.emplace_back(s3y);
        depoFutSwapInstruments.emplace_back(s5y);
        depoFutSwapInstruments.emplace_back(s10y);
        depoFutSwapInstruments.emplace_back(s15y);
        std::shared_ptr < YieldTermStructure > depoFutSwapTermStructure =
                std::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(
                        settlementDate, depoFutSwapInstruments,
                        termStructureDayCounter,
                        tolerance);


        // A depo-FRA-swap curve
        std::vector<std::shared_ptr<RateHelper> > depoFRASwapInstruments;
        depoFRASwapInstruments.emplace_back(d1w);
        depoFRASwapInstruments.emplace_back(d1m);
        depoFRASwapInstruments.emplace_back(d3m);
        depoFRASwapInstruments.emplace_back(fra3x6);
        depoFRASwapInstruments.emplace_back(fra6x9);
        depoFRASwapInstruments.emplace_back(fra6x12);
        depoFRASwapInstruments.emplace_back(s2y);
        depoFRASwapInstruments.emplace_back(s3y);
        depoFRASwapInstruments.emplace_back(s5y);
        depoFRASwapInstruments.emplace_back(s10y);
        depoFRASwapInstruments.emplace_back(s15y);
        std::shared_ptr < YieldTermStructure > depoFRASwapTermStructure =
                std::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(
                        settlementDate, depoFRASwapInstruments,
                        termStructureDayCounter,
                        tolerance);


        // Term structures that will be used for pricing:
        // the one used for discounting cash flows
        RelinkableHandle<YieldTermStructure> discountingTermStructure;
        // the one used for forward rate forecasting
        RelinkableHandle<YieldTermStructure> forecastingTermStructure;


        /*********************
        * SWAPS TO BE PRICED *
        **********************/

        // constant nominal 1,000,000 Euro
        Real nominal = 1000000.0;
        // fixed leg
        Frequency fixedLegFrequency = Annual;
        BusinessDayConvention fixedLegConvention = Unadjusted;
        BusinessDayConvention floatingLegConvention = ModifiedFollowing;
        DayCounter fixedLegDayCounter = Thirty360(Thirty360::European);
        Rate fixedRate = 0.04;
        DayCounter floatingLegDayCounter = Actual360();

        // floating leg
        Frequency floatingLegFrequency = Semiannual;
        std::shared_ptr < IborIndex > euriborIndex =
                std::make_shared<Euribor6M>(forecastingTermStructure);
        Spread spread = 0.0;

        Integer lenghtInYears = 5;
        VanillaSwap::Type swapType = VanillaSwap::Payer;

        Date maturity = settlementDate + lenghtInYears * Years;
        Schedule fixedSchedule(settlementDate, maturity,
                               Period(fixedLegFrequency),
                               calendar, fixedLegConvention,
                               fixedLegConvention,
                               DateGeneration::Forward, false);
        Schedule floatSchedule(settlementDate, maturity,
                               Period(floatingLegFrequency),
                               calendar, floatingLegConvention,
                               floatingLegConvention,
                               DateGeneration::Forward, false);
        VanillaSwap spot5YearSwap(swapType, nominal,
                                  fixedSchedule, fixedRate, fixedLegDayCounter,
                                  floatSchedule, euriborIndex, spread,
                                  floatingLegDayCounter);

        Date fwdStart = calendar.advance(settlementDate, 1, Years);
        Date fwdMaturity = fwdStart + lenghtInYears * Years;
        Schedule fwdFixedSchedule(fwdStart, fwdMaturity,
                                  Period(fixedLegFrequency),
                                  calendar, fixedLegConvention,
                                  fixedLegConvention,
                                  DateGeneration::Forward, false);
        Schedule fwdFloatSchedule(fwdStart, fwdMaturity,
                                  Period(floatingLegFrequency),
                                  calendar, floatingLegConvention,
                                  floatingLegConvention,
                                  DateGeneration::Forward, false);
        VanillaSwap oneYearForward5YearSwap(swapType, nominal,
                                            fwdFixedSchedule, fixedRate, fixedLegDayCounter,
                                            fwdFloatSchedule, euriborIndex, spread,
                                            floatingLegDayCounter);


        /***************
        * SWAP PRICING *
        ****************/

        // utilities for reporting
        std::vector<std::string> headers(4);
        headers[0] = "term structure";
        headers[1] = "net present value";
        headers[2] = "fair spread";
        headers[3] = "fair fixed rate";
        std::string separator = " | ";
        Size width = headers[0].size() + separator.size()
                     + headers[1].size() + separator.size()
                     + headers[2].size() + separator.size()
                     + headers[3].size() + separator.size() - 1;
        std::string rule(width, '-'), dblrule(width, '=');
        std::string tab(8, ' ');

        // calculations
        std::cout << dblrule << std::endl;
        std::cout << "5-year market swap-rate = "
                  << std::setprecision(2) << io::rate(s5yRate->value())
                  << std::endl;
        std::cout << dblrule << std::endl;

        std::cout << tab << "5-years swap paying "
                  << io::rate(fixedRate) << std::endl;
        std::cout << headers[0] << separator
                  << headers[1] << separator
                  << headers[2] << separator
                  << headers[3] << separator << std::endl;
        std::cout << rule << std::endl;

        Real NPV;
        Rate fairRate;
        Spread fairSpread;

        std::shared_ptr < PricingEngine > swapEngine =
                std::make_shared<DiscountingSwapEngine>(discountingTermStructure);

        spot5YearSwap.setPricingEngine(swapEngine);
        oneYearForward5YearSwap.setPricingEngine(swapEngine);

        // Of course, you're not forced to really use different curves
        forecastingTermStructure.linkTo(depoSwapTermStructure);
        discountingTermStructure.linkTo(depoSwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        // let's check that the 5 years swap has been correctly re-priced
        QL_REQUIRE(std::fabs(fairRate - s5yQuote) < 1e-8,
                   "5-years swap mispriced by "
                           << io::rate(std::fabs(fairRate - s5yQuote)));


        forecastingTermStructure.linkTo(depoFutSwapTermStructure);
        discountingTermStructure.linkTo(depoFutSwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-fut-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        QL_REQUIRE(std::fabs(fairRate - s5yQuote) < 1e-8,
                   "5-years swap mispriced!");


        forecastingTermStructure.linkTo(depoFRASwapTermStructure);
        discountingTermStructure.linkTo(depoFRASwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-FRA-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        QL_REQUIRE(std::fabs(fairRate - s5yQuote) < 1e-8,
                   "5-years swap mispriced!");


        std::cout << rule << std::endl;

        // now let's price the 1Y forward 5Y swap

        std::cout << tab << "5-years, 1-year forward swap paying "
                  << io::rate(fixedRate) << std::endl;
        std::cout << headers[0] << separator
                  << headers[1] << separator
                  << headers[2] << separator
                  << headers[3] << separator << std::endl;
        std::cout << rule << std::endl;


        forecastingTermStructure.linkTo(depoSwapTermStructure);
        discountingTermStructure.linkTo(depoSwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        forecastingTermStructure.linkTo(depoFutSwapTermStructure);
        discountingTermStructure.linkTo(depoFutSwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-fut-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        forecastingTermStructure.linkTo(depoFRASwapTermStructure);
        discountingTermStructure.linkTo(depoFRASwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-FRA-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        // now let's say that the 5-years swap rate goes up to 4.60%.
        // A smarter market element--say, connected to a data source-- would
        // notice the change itself. Since we're using SimpleQuotes,
        // we'll have to change the value manually--which forces us to
        // downcast the handle and use the SimpleQuote
        // interface. In any case, the point here is that a change in the
        // value contained in the Quote triggers a new bootstrapping
        // of the curve and a repricing of the swap.

        std::shared_ptr < SimpleQuote > fiveYearsRate =
                std::dynamic_pointer_cast<SimpleQuote>(s5yRate);
        fiveYearsRate->setValue(0.0460);

        std::cout << dblrule << std::endl;
        std::cout << "5-year market swap-rate = "
                  << io::rate(s5yRate->value()) << std::endl;
        std::cout << dblrule << std::endl;

        std::cout << tab << "5-years swap paying "
                  << io::rate(fixedRate) << std::endl;
        std::cout << headers[0] << separator
                  << headers[1] << separator
                  << headers[2] << separator
                  << headers[3] << separator << std::endl;
        std::cout << rule << std::endl;

        // now get the updated results
        forecastingTermStructure.linkTo(depoSwapTermStructure);
        discountingTermStructure.linkTo(depoSwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        QL_REQUIRE(std::fabs(fairRate - s5yRate->value()) < 1e-8,
                   "5-years swap mispriced!");


        forecastingTermStructure.linkTo(depoFutSwapTermStructure);
        discountingTermStructure.linkTo(depoFutSwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-fut-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        QL_REQUIRE(std::fabs(fairRate - s5yRate->value()) < 1e-8,
                   "5-years swap mispriced!");


        forecastingTermStructure.linkTo(depoFRASwapTermStructure);
        discountingTermStructure.linkTo(depoFRASwapTermStructure);

        NPV = spot5YearSwap.NPV();
        fairSpread = spot5YearSwap.fairSpread();
        fairRate = spot5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-FRA-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        QL_REQUIRE(std::fabs(fairRate - s5yRate->value()) < 1e-8,
                   "5-years swap mispriced!");

        std::cout << rule << std::endl;

        // the 1Y forward 5Y swap changes as well

        std::cout << tab << "5-years, 1-year forward swap paying "
                  << io::rate(fixedRate) << std::endl;
        std::cout << headers[0] << separator
                  << headers[1] << separator
                  << headers[2] << separator
                  << headers[3] << separator << std::endl;
        std::cout << rule << std::endl;


        forecastingTermStructure.linkTo(depoSwapTermStructure);
        discountingTermStructure.linkTo(depoSwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        forecastingTermStructure.linkTo(depoFutSwapTermStructure);
        discountingTermStructure.linkTo(depoFutSwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-fut-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;


        forecastingTermStructure.linkTo(depoFRASwapTermStructure);
        discountingTermStructure.linkTo(depoFRASwapTermStructure);

        NPV = oneYearForward5YearSwap.NPV();
        fairSpread = oneYearForward5YearSwap.fairSpread();
        fairRate = oneYearForward5YearSwap.fairRate();

        std::cout << std::setw(headers[0].size())
                  << "depo-FRA-swap" << separator;
        std::cout << std::setw(headers[1].size())
                  << std::fixed << std::setprecision(2) << NPV << separator;
        std::cout << std::setw(headers[2].size())
                  << io::rate(fairSpread) << separator;
        std::cout << std::setw(headers[3].size())
                  << io::rate(fairRate) << separator;
        std::cout << std::endl;

        std::chrono::time_point<std::chrono::steady_clock> endT = std::chrono::steady_clock::now();
        double seconds = static_cast<double>((endT - startT).count()) / 1.0e9;
        Integer hours = int(seconds / 3600);
        seconds -= hours * 3600;
        Integer minutes = int(seconds / 60);
        seconds -= minutes * 60;
        std::cout << " \nRun completed in ";
        if (hours > 0)
            std::cout << hours << " h ";
        if (hours > 0 || minutes > 0)
            std::cout << minutes << " m ";
        std::cout << std::fixed << std::setprecision(0)
                  << seconds << " s\n" << std::endl;

        return 0;

    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
        return 1;
    }
}

