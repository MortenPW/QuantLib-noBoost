/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2014 Jose Aparicio

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
#include <ql/experimental/credit/gaussianlhplossmodel.hpp>
#include <ql/experimental/credit/constantlosslatentmodel.hpp>
#include <ql/experimental/credit/binomiallossmodel.hpp>
#include <ql/experimental/credit/randomdefaultlatentmodel.hpp>
#include <ql/experimental/credit/randomlosslatentmodel.hpp>
#include <ql/experimental/credit/spotlosslatentmodel.hpp>
#include <ql/experimental/credit/basecorrelationlossmodel.hpp>
#include <ql/termstructures/credit/flathazardrate.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/currencies/europe.hpp>


#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>

using namespace std;
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

        Calendar calendar = TARGET();
        Date todaysDate(19, March, 2014);
        // must be a business day
        todaysDate = calendar.adjust(todaysDate);

        Settings::instance().evaluationDate() = todaysDate;


        /* --------------------------------------------------------------
                        SET UP BASKET PORTFOLIO
        -------------------------------------------------------------- */
        // build curves and issuers into a basket of ten names
        std::vector<Real> hazardRates;
        hazardRates = {
                //  0.01, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9;
                0.001, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09};
        //  0.01,  0.01,  0.01,  0.01, 0.01,  0.01,  0.01,  0.01,  0.01,  0.01;        
        std::vector<std::string> names;
        for (Size i = 0; i < hazardRates.size(); i++)
            names.emplace_back(std::string("Acme") +
                               to_string(i));
        std::vector<Handle<DefaultProbabilityTermStructure> > defTS;
        for (Size i = 0; i < hazardRates.size(); i++) {
            defTS.emplace_back(Handle<DefaultProbabilityTermStructure>(
                    std::make_shared<FlatHazardRate>(0, TARGET(), hazardRates[i],
                                                     Actual365Fixed())));
            defTS.back()->enableExtrapolation();
        }
        std::vector<Issuer> issuers;
        for (Size i = 0; i < hazardRates.size(); i++) {
            std::vector<QuantLib::Issuer::key_curve_pair> curves(1,
                                                                 std::make_pair(NorthAmericaCorpDefaultKey(
                                                                         EURCurrency(), QuantLib::SeniorSec,
                                                                         Period(), 1. // amount threshold
                                                                 ), defTS[i]));
            issuers.emplace_back(Issuer(curves));
        }

        std::shared_ptr < Pool > thePool = std::make_shared<Pool>();
        for (Size i = 0; i < hazardRates.size(); i++)
            thePool->add(names[i], issuers[i], NorthAmericaCorpDefaultKey(
                    EURCurrency(), QuantLib::SeniorSec, Period(), 1.));

        std::vector<DefaultProbKey> defaultKeys(hazardRates.size(),
                                                NorthAmericaCorpDefaultKey(EURCurrency(), SeniorSec, Period(), 1.));
        std::shared_ptr < Basket > theBskt = std::make_shared<Basket>(
                todaysDate,
                names, std::vector<Real>(hazardRates.size(), 100.), thePool,
                //   0.0, 0.78);
                0.03, .06);

        /* --------------------------------------------------------------
                        SET UP DEFAULT LOSS MODELS
        -------------------------------------------------------------- */

        std::vector<Real> recoveries(hazardRates.size(), 0.4);

        Date calcDate(TARGET().advance(Settings::instance().evaluationDate(),
                                       Period(60, Months)));
        Real factorValue = 0.05;
        std::vector<std::vector<Real> > fctrsWeights(hazardRates.size(),
                                                     std::vector<Real>(1, std::sqrt(factorValue)));

        // --- LHP model --------------------------
        std::shared_ptr < DefaultLossModel > lmGLHP(
                std::make_shared<GaussianLHPLossModel>(
                        fctrsWeights[0][0] * fctrsWeights[0][0], recoveries));
        theBskt->setLossModel(lmGLHP);

        std::cout << "GLHP Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- G Binomial model --------------------
        std::shared_ptr < GaussianConstantLossLM > ktLossLM(
                std::make_shared<GaussianConstantLossLM>(fctrsWeights,
                                                         recoveries, LatentModelIntegrationType::GaussianQuadrature,
                                                         GaussianCopulaPolicy::initTraits()));
        std::shared_ptr < DefaultLossModel > lmBinomial(
                std::make_shared<GaussianBinomialLossModel>(ktLossLM));
        theBskt->setLossModel(lmBinomial);

        std::cout << "Gaussian Binomial Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- T Binomial model --------------------
        TCopulaPolicy::initTraits initT;
        initT.tOrders = std::vector<Integer>(2, 3);
        std::shared_ptr < TConstantLossLM > ktTLossLM(
                std::make_shared<TConstantLossLM>(fctrsWeights,
                                                  recoveries,
                        //LatentModelIntegrationType::GaussianQuadrature,
                                                  LatentModelIntegrationType::Trapezoid,
                                                  initT));
        std::shared_ptr < DefaultLossModel > lmTBinomial(
                std::make_shared<TBinomialLossModel>(ktTLossLM));
        theBskt->setLossModel(lmTBinomial);

        std::cout << "T Binomial Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- G Inhomogeneous model ---------------
        Size numSimulations = 100000;
        std::shared_ptr < GaussianConstantLossLM > gLM(
                std::make_shared<GaussianConstantLossLM>(fctrsWeights,
                                                         recoveries,
                                                         LatentModelIntegrationType::GaussianQuadrature,
                        // g++ requires this when using make_shared
                                                         GaussianCopulaPolicy::initTraits()));

        Size numBuckets = 100;
        std::shared_ptr < DefaultLossModel > inhomogeneousLM(
                std::make_shared<IHGaussPoolLossModel>(gLM, numBuckets));
        theBskt->setLossModel(inhomogeneousLM);

        std::cout << "G Inhomogeneous Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- G Random model ---------------------
        // Gaussian random joint default model:
        // Size numCoresUsed = 4;
        // Sobol, many cores
        std::shared_ptr < DefaultLossModel > rdlmG(
                std::make_shared<RandomDefaultLM<GaussianCopulaPolicy,
                        RandomSequenceGenerator<
                                BoxMullerGaussianRng<MersenneTwisterUniformRng> > > >(gLM,
                                                                                      recoveries, numSimulations, 1.e-6,
                                                                                      2863311530));
        //std::shared_ptr<DefaultLossModel> rdlmG(
        //    std::make_shared<RandomDefaultLM<GaussianCopulaPolicy> >(gLM, 
        //        recoveries, numSimulations, 1.e-6, 2863311530));
        theBskt->setLossModel(rdlmG);

        std::cout << "Random G Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- StudentT Random model ---------------------
        // Sobol, many cores
        std::shared_ptr < DefaultLossModel > rdlmT(
                std::make_shared<RandomDefaultLM<TCopulaPolicy,
                        RandomSequenceGenerator<
                                PolarStudentTRng<MersenneTwisterUniformRng> > > >(ktTLossLM,
                                                                                  recoveries, numSimulations, 1.e-6,
                                                                                  2863311530));
        //std::shared_ptr<DefaultLossModel> rdlmT(
        //    std::make_shared<RandomDefaultLM<TCopulaPolicy> >(ktTLossLM, 
        //        recoveries, numSimulations, 1.e-6, 2863311530));
        theBskt->setLossModel(rdlmT);

        std::cout << "Random T Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;


        // Spot Loss latent model: 
        std::vector<std::vector<Real> > fctrsWeightsRR(2 * hazardRates.size(),
                                                       std::vector<Real>(1, std::sqrt(factorValue)));
        Real modelA = 2.2;
        std::shared_ptr < GaussianSpotLossLM > sptLG = std::make_shared<GaussianSpotLossLM>(
                fctrsWeightsRR, recoveries, modelA,
                LatentModelIntegrationType::GaussianQuadrature,
                GaussianCopulaPolicy::initTraits());
        std::shared_ptr < TSpotLossLM > sptLT = std::make_shared<TSpotLossLM>(fctrsWeightsRR,
                                                                              recoveries, modelA,
                                                                              LatentModelIntegrationType::GaussianQuadrature,
                                                                              initT);


        // --- G Random Loss model ---------------------
        // Gaussian random joint default model:
        // Sobol, many cores
        std::shared_ptr < DefaultLossModel > rdLlmG(
                std::make_shared<RandomLossLM<GaussianCopulaPolicy> >(sptLG,
                                                                      numSimulations, 1.e-6, 2863311530));
        theBskt->setLossModel(rdLlmG);

        std::cout << "Random Loss G Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // --- T Random Loss model ---------------------
        // Gaussian random joint default model:
        // Sobol, many cores
        std::shared_ptr < DefaultLossModel > rdLlmT(
                std::make_shared<RandomLossLM<TCopulaPolicy> >(sptLT,
                                                               numSimulations, 1.e-6, 2863311530));
        theBskt->setLossModel(rdLlmT);

        std::cout << "Random Loss T Expected 10-Yr Losses: " << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        // Base Correlation model set up to test cocherence with base LHP model
        std::vector<Period> bcTenors;
        bcTenors.emplace_back(Period(1, Years));
        bcTenors.emplace_back(Period(5, Years));
        std::vector<Real> bcLossPercentages;
        bcLossPercentages.emplace_back(0.03);
        bcLossPercentages.emplace_back(0.12);
        std::vector<std::vector<Handle<Quote> > > correls;
        // 
        std::vector<Handle<Quote> > corr1Y;
        // 3%
        corr1Y.emplace_back(Handle<Quote>(std::make_shared<SimpleQuote>(fctrsWeights[0][0] * fctrsWeights[0][0])));
        // 12%
        corr1Y.emplace_back(Handle<Quote>(std::make_shared<SimpleQuote>(fctrsWeights[0][0] * fctrsWeights[0][0])));
        correls.emplace_back(corr1Y);
        std::vector<Handle<Quote> > corr2Y;
        // 3%
        corr2Y.emplace_back(Handle<Quote>(std::make_shared<SimpleQuote>(fctrsWeights[0][0] * fctrsWeights[0][0])));
        // 12%
        corr2Y.emplace_back(Handle<Quote>(std::make_shared<SimpleQuote>(fctrsWeights[0][0] * fctrsWeights[0][0])));
        correls.emplace_back(corr2Y);
        std::shared_ptr<BaseCorrelationTermStructure<BilinearInterpolation>> correlSurface =
                std::make_shared<BaseCorrelationTermStructure<BilinearInterpolation>>(
                        // first one would do, all should be the same.
                        defTS[0]->settlementDays(),
                        defTS[0]->calendar(),
                        Unadjusted,
                        bcTenors,
                        bcLossPercentages,
                        correls,
                        Actual365Fixed()
                );
        Handle<BaseCorrelationTermStructure<BilinearInterpolation> >
                correlHandle(correlSurface);
        std::shared_ptr < DefaultLossModel > bcLMG_LHP_Bilin(
                std::make_shared<GaussianLHPFlatBCLM>(correlHandle, recoveries,
                                                      GaussianCopulaPolicy::initTraits()));

        theBskt->setLossModel(bcLMG_LHP_Bilin);

        std::cout << "Base Correlation GLHP Expected 10-Yr Losses: "
                  << std::endl;
        std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

        std::chrono::time_point<std::chrono::steady_clock> endT = std::chrono::steady_clock::now();
        double elapsed = static_cast<double>((endT - startT).count()) / 1.0e9;
        Real seconds = elapsed;
        Integer hours = Integer(seconds / 3600);
        seconds -= hours * 3600;
        Integer minutes = Integer(seconds / 60);
        seconds -= minutes * 60;
        cout << "Run completed in ";
        if (hours > 0)
            cout << hours << " h ";
        if (hours > 0 || minutes > 0)
            cout << minutes << " m ";
        cout << fixed << setprecision(0)
             << seconds << " s" << endl;

        return 0;
    } catch (exception &e) {
        cerr << e.what() << endl;
        return 1;
    } catch (...) {
        cerr << "unknown error" << endl;
        return 1;
    }
}

