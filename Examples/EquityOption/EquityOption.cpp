/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*!
 Copyright (C) 2005, 2006, 2007, 2009 StatPro Italia srl

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

#include <ql/qldefines.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/pricingengines/vanilla/binomialengine.hpp>
#include <ql/pricingengines/vanilla/analyticeuropeanengine.hpp>
#include <ql/pricingengines/vanilla/analytichestonengine.hpp>
#include <ql/pricingengines/vanilla/baroneadesiwhaleyengine.hpp>
#include <ql/pricingengines/vanilla/bjerksundstenslandengine.hpp>
#include <ql/pricingengines/vanilla/batesengine.hpp>
#include <ql/pricingengines/vanilla/integralengine.hpp>
#include <ql/pricingengines/vanilla/fdeuropeanengine.hpp>
#include <ql/pricingengines/vanilla/fdbermudanengine.hpp>
#include <ql/pricingengines/vanilla/fdamericanengine.hpp>
#include <ql/pricingengines/vanilla/mceuropeanengine.hpp>
#include <ql/pricingengines/vanilla/mcamericanengine.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/utilities/dataformatters.hpp>

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

        // set up dates
        Calendar calendar = TARGET();
        Date todaysDate(15, May, 1998);
        Date settlementDate(17, May, 1998);
        Settings::instance().evaluationDate() = todaysDate;

        // our options
        Option::Type type(Option::Put);
        Real underlying = 36;
        Real strike = 40;
        Spread dividendYield = 0.00;
        Rate riskFreeRate = 0.06;
        Volatility volatility = 0.20;
        Date maturity(17, May, 1999);
        DayCounter dayCounter = Actual365Fixed();

        std::cout << "Option type = " << type << std::endl;
        std::cout << "Maturity = " << maturity << std::endl;
        std::cout << "Underlying price = " << underlying << std::endl;
        std::cout << "Strike = " << strike << std::endl;
        std::cout << "Risk-free interest rate = " << io::rate(riskFreeRate)
                  << std::endl;
        std::cout << "Dividend yield = " << io::rate(dividendYield)
                  << std::endl;
        std::cout << "Volatility = " << io::volatility(volatility)
                  << std::endl;
        std::cout << std::endl;
        std::string method;
        std::cout << std::endl;

        // write column headings
        Size widths[] = {35, 14, 14, 14};
        std::cout << std::setw(widths[0]) << std::left << "Method"
                  << std::setw(widths[1]) << std::left << "European"
                  << std::setw(widths[2]) << std::left << "Bermudan"
                  << std::setw(widths[3]) << std::left << "American"
                  << std::endl;

        std::vector<Date> exerciseDates;
        for (Integer i = 1; i <= 4; i++)
            exerciseDates.emplace_back(settlementDate + 3 * i * Months);

        std::shared_ptr < Exercise > europeanExercise =
                std::make_shared<EuropeanExercise>(maturity);

        std::shared_ptr < Exercise > bermudanExercise =
                std::make_shared<BermudanExercise>(exerciseDates);

        std::shared_ptr < Exercise > americanExercise =
                std::make_shared<AmericanExercise>(settlementDate,
                                                   maturity);

        Handle<Quote> underlyingH(
                std::make_shared<SimpleQuote>(underlying));

        // bootstrap the yield/dividend/vol curves
        Handle<YieldTermStructure> flatTermStructure(
                std::make_shared<FlatForward>(settlementDate, riskFreeRate, dayCounter));
        Handle<YieldTermStructure> flatDividendTS(
                std::make_shared<FlatForward>(settlementDate, dividendYield, dayCounter));
        Handle<BlackVolTermStructure> flatVolTS(
                std::make_shared<BlackConstantVol>(settlementDate, calendar, volatility,
                                                   dayCounter));
        std::shared_ptr < StrikedTypePayoff > payoff =
                std::make_shared<PlainVanillaPayoff>(type, strike);
        std::shared_ptr < BlackScholesMertonProcess > bsmProcess =
                std::make_shared<BlackScholesMertonProcess>(underlyingH, flatDividendTS,
                                                            flatTermStructure, flatVolTS);

        // options
        VanillaOption europeanOption(payoff, europeanExercise);
        VanillaOption bermudanOption(payoff, bermudanExercise);
        VanillaOption americanOption(payoff, americanExercise);

        // Analytic formulas:

        // Black-Scholes for European
        method = "Black-Scholes";
        europeanOption.setPricingEngine(std::make_shared<AnalyticEuropeanEngine>(bsmProcess));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // semi-analytic Heston for European
        method = "Heston semi-analytic";
        std::shared_ptr < HestonProcess > hestonProcess =
                std::make_shared<HestonProcess>(flatTermStructure, flatDividendTS,
                                                underlyingH, volatility * volatility,
                                                1.0, volatility * volatility, 0.001, 0.0);
        std::shared_ptr < HestonModel > hestonModel =
                std::make_shared<HestonModel>(hestonProcess);
        europeanOption.setPricingEngine(std::make_shared<AnalyticHestonEngine>(hestonModel));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // semi-analytic Bates for European
        method = "Bates semi-analytic";
        std::shared_ptr < BatesProcess > batesProcess =
                std::make_shared<BatesProcess>(flatTermStructure, flatDividendTS,
                                               underlyingH, volatility * volatility,
                                               1.0, volatility * volatility, 0.001, 0.0,
                                               1e-14, 1e-14, 1e-14);
        std::shared_ptr < BatesModel > batesModel = std::make_shared<BatesModel>(batesProcess);
        europeanOption.setPricingEngine(std::make_shared<BatesEngine>(batesModel));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // Barone-Adesi and Whaley approximation for American
        method = "Barone-Adesi/Whaley";
        americanOption.setPricingEngine(std::make_shared<BaroneAdesiWhaleyApproximationEngine>(bsmProcess));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << "N/A"
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Bjerksund and Stensland approximation for American
        method = "Bjerksund/Stensland";
        americanOption.setPricingEngine(std::make_shared<BjerksundStenslandApproximationEngine>(bsmProcess));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << "N/A"
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Integral
        method = "Integral";
        europeanOption.setPricingEngine(std::make_shared<IntegralEngine>(bsmProcess));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // Finite differences
        Size timeSteps = 801;
        method = "Finite differences";
        europeanOption.setPricingEngine(std::make_shared<FDEuropeanEngine<CrankNicolson>>(bsmProcess,
                                                                                          timeSteps,
                                                                                          timeSteps - 1));
        bermudanOption.setPricingEngine(std::make_shared<FDBermudanEngine<CrankNicolson>>(bsmProcess,
                                                                                          timeSteps,
                                                                                          timeSteps - 1));
        americanOption.setPricingEngine(std::make_shared<FDAmericanEngine<CrankNicolson>>(bsmProcess,
                                                                                          timeSteps,
                                                                                          timeSteps - 1));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Jarrow-Rudd
        method = "Binomial Jarrow-Rudd";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<JarrowRudd>>(bsmProcess,
                                                                                            timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<JarrowRudd>>(bsmProcess,
                                                                                            timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<JarrowRudd>>(bsmProcess,
                                                                                            timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;
        method = "Binomial Cox-Ross-Rubinstein";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<CoxRossRubinstein>>(bsmProcess,
                                                                                                   timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<CoxRossRubinstein>>(bsmProcess,
                                                                                                   timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<CoxRossRubinstein>>(bsmProcess,
                                                                                                   timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Additive equiprobabilities
        method = "Additive equiprobabilities";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<AdditiveEQPBinomialTree>>(
                bsmProcess,
                timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<AdditiveEQPBinomialTree>>(
                bsmProcess,
                timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<AdditiveEQPBinomialTree>>(
                bsmProcess,
                timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Binomial Trigeorgis
        method = "Binomial Trigeorgis";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Trigeorgis>>(bsmProcess,
                                                                                            timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Trigeorgis>>(bsmProcess,
                                                                                            timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Trigeorgis>>(bsmProcess,
                                                                                            timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Binomial Tian
        method = "Binomial Tian";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Tian>>(bsmProcess, timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Tian>>(bsmProcess, timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Tian>>(bsmProcess, timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Binomial Leisen-Reimer
        method = "Binomial Leisen-Reimer";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<LeisenReimer>>(bsmProcess,
                                                                                              timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<LeisenReimer>>(bsmProcess,
                                                                                              timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<LeisenReimer>>(bsmProcess,
                                                                                              timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Binomial method: Binomial Joshi
        method = "Binomial Joshi";
        europeanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Joshi4>>(bsmProcess, timeSteps));
        bermudanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Joshi4>>(bsmProcess, timeSteps));
        americanOption.setPricingEngine(std::make_shared<BinomialVanillaEngine<Joshi4>>(bsmProcess, timeSteps));
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << bermudanOption.NPV()
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // Monte Carlo Method: MC (crude)
        timeSteps = 1;
        method = "MC (crude)";
        Size mcSeed = 42;
        std::shared_ptr < PricingEngine > mcengine1;
        mcengine1 = MakeMCEuropeanEngine<PseudoRandom>(bsmProcess)
                .withSteps(timeSteps)
                .withAbsoluteTolerance(0.02)
                .withSeed(mcSeed);
        europeanOption.setPricingEngine(mcengine1);
        // Real errorEstimate = europeanOption.errorEstimate();
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // Monte Carlo Method: QMC (Sobol)
        method = "QMC (Sobol)";
        Size nSamples = 32768;  // 2^15

        std::shared_ptr < PricingEngine > mcengine2;
        mcengine2 = MakeMCEuropeanEngine<LowDiscrepancy>(bsmProcess)
                .withSteps(timeSteps)
                .withSamples(nSamples);
        europeanOption.setPricingEngine(mcengine2);
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << europeanOption.NPV()
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << "N/A"
                  << std::endl;

        // Monte Carlo Method: MC (Longstaff Schwartz)
        method = "MC (Longstaff Schwartz)";
        std::shared_ptr < PricingEngine > mcengine3;
        mcengine3 = MakeMCAmericanEngine<PseudoRandom>(bsmProcess)
                .withSteps(100)
                .withAntitheticVariate()
                .withCalibrationSamples(4096)
                .withAbsoluteTolerance(0.02)
                .withSeed(mcSeed);
        americanOption.setPricingEngine(mcengine3);
        std::cout << std::setw(widths[0]) << std::left << method
                  << std::fixed
                  << std::setw(widths[1]) << std::left << "N/A"
                  << std::setw(widths[2]) << std::left << "N/A"
                  << std::setw(widths[3]) << std::left << americanOption.NPV()
                  << std::endl;

        // End test
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
