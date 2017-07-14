/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2007 Ferdinando Ametrano
 Copyright (C) 2007 Marco Bianchetti
 Copyright (C) 2007 Cristina Duminuco
 Copyright (C) 2007 Mark Joshi

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

#include <ql/math/optimization/spherecylinder.hpp>
#include <ql/models/marketmodels/models/fwdtocotswapadapter.hpp>
#include <ql/models/marketmodels/models/fwdperiodadapter.hpp>
#include <ql/models/marketmodels/models/capletcoterminalmaxhomogeneity.hpp>
#include <ql/models/marketmodels/models/alphaformconcrete.hpp>
#include <ql/models/marketmodels/correlations/cotswapfromfwdcorrelation.hpp>
#include <ql/models/marketmodels/correlations/timehomogeneousforwardcorrelation.hpp>
#include <ql/models/marketmodels/models/piecewiseconstantabcdvariance.hpp>
#include <ql/models/marketmodels/models/capletcoterminalswaptioncalibration.hpp>
#include <ql/models/marketmodels/models/cotswaptofwdadapter.hpp>
#include <ql/models/marketmodels/models/pseudorootfacade.hpp>
#include <ql/models/marketmodels/products/multistep/multistepcoterminalswaps.hpp>
#include <ql/models/marketmodels/products/multistep/multistepcoterminalswaptions.hpp>
#include <ql/models/marketmodels/products/multistep/multistepswap.hpp>
#include <ql/models/marketmodels/products/multiproductcomposite.hpp>
#include <ql/models/marketmodels/accountingengine.hpp>
#include <ql/models/marketmodels/utilities.hpp>
#include <ql/models/marketmodels/evolvers/lognormalcotswapratepc.hpp>
#include <ql/models/marketmodels/evolvers/lognormalfwdratepc.hpp>
#include <ql/models/marketmodels/correlations/expcorrelations.hpp>
#include <ql/models/marketmodels/models/flatvol.hpp>
#include <ql/models/marketmodels/models/abcdvol.hpp>
#include <ql/models/marketmodels/browniangenerators/mtbrowniangenerator.hpp>
#include <ql/models/marketmodels/browniangenerators/sobolbrowniangenerator.hpp>
#include <ql/models/marketmodels/swapforwardmappings.hpp>
#include <ql/models/marketmodels/curvestates/coterminalswapcurvestate.hpp>
#include <ql/methods/montecarlo/genericlsregression.hpp>
#include <ql/legacy/libormarketmodels/lmlinexpcorrmodel.hpp>
#include <ql/legacy/libormarketmodels/lmextlinexpvolmodel.hpp>
#include <ql/time/schedule.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/simpledaycounter.hpp>
#include <ql/pricingengines/blackformula.hpp>
#include <ql/pricingengines/blackcalculator.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <ql/math/integrals/segmentintegral.hpp>
#include <ql/math/statistics/convergencestatistics.hpp>
#include <ql/math/functional.hpp>
#include <ql/math/optimization/simplex.hpp>
#include <ql/math/statistics/sequencestatistics.hpp>
#include <sstream>
#include <ql/models/marketmodels/models/capletcoterminalperiodic.hpp>

#include <ql/models/marketmodels/models/volatilityinterpolationspecifierabcd.hpp>

#include <float.h>

using namespace QuantLib;


using std::fabs;
using std::sqrt;

#define BEGIN(x) (x+0)
#define END(x) (x+LENGTH(x))

namespace {

    Date todaysDate_, startDate_, endDate_;
    std::vector<Time> rateTimes_;
    std::vector<Real> accruals_;
    Calendar calendar_;
    DayCounter dayCounter_;
    std::vector<Rate> todaysForwards_, todaysSwaps_;
    std::vector<Real> coterminalAnnuity_;
    Size numberOfFactors_;
    Real alpha_, alphaMax_, alphaMin_;
    Spread displacement_;
    std::vector<DiscountFactor> todaysDiscounts_;
    std::vector<Volatility> swaptionDisplacedVols_, swaptionVols_;
    std::vector<Volatility> capletDisplacedVols_, capletVols_;
    Real a_, b_, c_, d_;
    Real longTermCorrelation_, beta_;
    Size measureOffset_;
    unsigned long seed_;
    Size paths_, trainingPaths_;
    bool printReport_ = false;

    void setup() {

        // Times
        calendar_ = NullCalendar();
        todaysDate_ = Settings::instance().evaluationDate();
        //startDate = todaysDate + 5*Years;
        endDate_ = todaysDate_ + 66*Months;
        Schedule dates(todaysDate_, endDate_, Period(Semiannual),
                       calendar_, Following, Following, DateGeneration::Backward, false);
        rateTimes_ = std::vector<Time>(dates.size()-1);
        accruals_ = std::vector<Real>(rateTimes_.size()-1);
        dayCounter_ = SimpleDayCounter();
        for (Size i=1; i<dates.size(); ++i)
            rateTimes_[i-1] = dayCounter_.yearFraction(todaysDate_, dates[i]);
        for (Size i=1; i<rateTimes_.size(); ++i)
            accruals_[i-1] = rateTimes_[i] - rateTimes_[i-1];

        // Rates & displacement
        todaysForwards_ = std::vector<Rate>(accruals_.size());
        numberOfFactors_ = 3;
        alpha_ = 0.0;
        alphaMax_ = 1.0;
        alphaMin_ = -1.0;
        displacement_ = 0.0;
        for (Size i=0; i<todaysForwards_.size(); ++i) {
            // FLOATING_POINT_EXCEPTION
            todaysForwards_[i] = 0.03 + 0.0025*i;
            //    todaysForwards_[i] = 0.03;
        }
        LMMCurveState curveState_lmm(rateTimes_);
        curveState_lmm.setOnForwardRates(todaysForwards_);
        todaysSwaps_ = curveState_lmm.coterminalSwapRates();

        // Discounts
        todaysDiscounts_ = std::vector<DiscountFactor>(rateTimes_.size());
        todaysDiscounts_[0] = 0.95;
        for (Size i=1; i<rateTimes_.size(); ++i)
            todaysDiscounts_[i] = todaysDiscounts_[i-1] /
                (1.0+todaysForwards_[i-1]*accruals_[i-1]);

        //// Swaption Volatilities
        //Volatility mktSwaptionVols[] = {
        //                        0.15541283,
        //                        0.18719678,
        //                        0.20890740,
        //                        0.22318179,
        //                        0.23212717,
        //                        0.23731450,
        //                        0.23988649,
        //                        0.24066384,
        //                        0.24023111,
        //                        0.23900189,
        //                        0.23726699,
        //                        0.23522952,
        //                        0.23303022,
        //                        0.23076564,
        //                        0.22850101,
        //                        0.22627951,
        //                        0.22412881,
        //                        0.22206569,
        //                        0.22009939
        //};

        //a = -0.0597;
        //b =  0.1677;
        //c =  0.5403;
        //d =  0.1710;

        a_ = 0.0;
        b_ = 0.17;
        c_ = 1.0;
        d_ = 0.10;

        Volatility mktCapletVols[] = {
            0.1640,
            0.1740,
            0.1840,
            0.1940,
            0.1840,
            0.1740,
            0.1640,
            0.1540,
            0.1440,
            0.1340376439125532
        };

        //swaptionDisplacedVols = std::vector<Volatility>(todaysSwaps.size());
        //swaptionVols = std::vector<Volatility>(todaysSwaps.size());
        //capletDisplacedVols = std::vector<Volatility>(todaysSwaps.size());
        capletVols_.resize(todaysSwaps_.size());
        for (Size i=0; i<todaysSwaps_.size(); i++) {
            //    swaptionDisplacedVols[i] = todaysSwaps[i]*mktSwaptionVols[i]/
            //                              (todaysSwaps[i]+displacement);
            //    swaptionVols[i]= mktSwaptionVols[i];
            //    capletDisplacedVols[i] = todaysForwards[i]*mktCapletVols[i]/
            //                            (todaysForwards[i]+displacement);
            capletVols_[i]= mktCapletVols[i];
        }

        // Cap/Floor Correlation
        longTermCorrelation_ = 0.5;
        beta_ = 0.2;
        measureOffset_ = 5;

        // Monte Carlo
        seed_ = 42;

#ifdef _DEBUG
        paths_ = 127;
        trainingPaths_ = 31;
#else
        paths_ = 32767; //262144-1; //; // 2^15-1
        trainingPaths_ = 8191; // 2^13-1
#endif
    }

    enum MarketModelType { ExponentialCorrelationFlatVolatility,
                           ExponentialCorrelationAbcdVolatility/*,
                           CalibratedMM*/
    };

    enum MeasureType { ProductSuggested, Terminal,
                       MoneyMarket, MoneyMarketPlus };

    enum EvolverType { Ipc, Pc , NormalPc };

}



TEST_CASE("MarketModelSmmCapletHomoCalibration_Function", "[MarketModelSmmCapletHomoCalibration]") {

    INFO("Testing max homogeneity caplet calibration "
                       "in a lognormal coterminal swap market model...");

    setup();

    Size numberOfRates = todaysForwards_.size();

    EvolutionDescription evolution(rateTimes_);
    // Size numberOfSteps = evolution.numberOfSteps();

    std::shared_ptr<PiecewiseConstantCorrelation> fwdCorr =
        std::make_shared<ExponentialForwardCorrelation>(rateTimes_,
                                      longTermCorrelation_,
                                      beta_);

    std::shared_ptr<LMMCurveState> cs = std::make_shared<LMMCurveState>(rateTimes_);
    cs->setOnForwardRates(todaysForwards_);

    std::shared_ptr<PiecewiseConstantCorrelation> corr =
        std::make_shared<CotSwapFromFwdCorrelation>(fwdCorr, *cs, displacement_);

    std::vector<std::shared_ptr<PiecewiseConstantVariance> >
                                    swapVariances(numberOfRates);
    for (Size i=0; i<numberOfRates; ++i) {
        swapVariances[i] = std::make_shared<PiecewiseConstantAbcdVariance>(a_, b_, c_, d_,
                                          i, rateTimes_);
    }

    // create calibrator
    Real caplet0Swaption1Priority = 1.0;
    if (printReport_) {
        INFO("caplet market vols: " << QL_FIXED <<
                           std::setprecision(4) << io::sequence(capletVols_));
        INFO("caplet0Swapt1Prior: " << caplet0Swaption1Priority);
    }
    CTSMMCapletMaxHomogeneityCalibration calibrator(evolution,
                                                    corr,
                                                    swapVariances,
                                                    capletVols_,
                                                    cs,
                                                    displacement_,
                                                    caplet0Swaption1Priority);
    // calibrate
    Natural maxIterations = 10;
    Real capletTolerance = 1e-4; // i.e. 1 bp
    Natural innerMaxIterations = 100;
    Real innerTolerance = 1e-8;
    if (printReport_) {
        INFO("numberOfFactors:    " << numberOfFactors_);
        INFO("maxIterations:      " << maxIterations);
        INFO("capletTolerance:    " << io::rate(capletTolerance));
        INFO("innerMaxIterations: " << innerMaxIterations);
        INFO("innerTolerance:     " << io::rate(innerTolerance));
    }
    bool result = calibrator.calibrate(numberOfFactors_,
                                       maxIterations,
                                       capletTolerance,
                                       innerMaxIterations,
                                       innerTolerance);
    if (!result)
        FAIL_CHECK("calibration failed");

    const std::vector<Matrix>& swapPseudoRoots = calibrator.swapPseudoRoots();
    std::shared_ptr<MarketModel> smm =
        std::make_shared<PseudoRootFacade>(swapPseudoRoots,
                         rateTimes_,
                         cs->coterminalSwapRates(),
                         std::vector<Spread>(numberOfRates, displacement_));
    std::shared_ptr<MarketModel> flmm = std::make_shared<CotSwapToFwdAdapter>(smm);
    Matrix capletTotCovariance = flmm->totalCovariance(numberOfRates-1);

    std::vector<Volatility> capletVols(numberOfRates);
    for (Size i=0; i<numberOfRates; ++i) {
        capletVols[i] = std::sqrt(capletTotCovariance[i][i]/rateTimes_[i]);
    }
    if (printReport_) {
        INFO("caplet smm implied vols: " << QL_FIXED <<
                           std::setprecision(4) << io::sequence(capletVols));
        INFO("failures: " << calibrator.failures());
        INFO("deformationSize: " << calibrator.deformationSize());
        INFO("capletRmsError: " << calibrator.capletRmsError());
        INFO("capletMaxError: " << calibrator.capletMaxError());
        INFO("swaptionRmsError: " << calibrator.swaptionRmsError());
        INFO("swaptionMaxError: " << calibrator.swaptionMaxError());
      }

    // check perfect swaption fit
    Real error, swapTolerance = 1e-14;
    Matrix swapTerminalCovariance(numberOfRates, numberOfRates, 0.0);
    for (Size i=0; i<numberOfRates; ++i) {
        Volatility expSwaptionVol = swapVariances[i]->totalVolatility(i);
        swapTerminalCovariance += swapPseudoRoots[i] * transpose(swapPseudoRoots[i]);
        Volatility swaptionVol = std::sqrt(swapTerminalCovariance[i][i]/rateTimes_[i]);
        error = std::fabs(swaptionVol-expSwaptionVol);
        if (error>swapTolerance)
            FAIL_CHECK("failed to reproduce " << io::ordinal(i+1) << " swaption vol:"
                        "\n expected:  " << io::rate(expSwaptionVol) <<
                        "\n realized:  " << io::rate(swaptionVol) <<
                        "\n error:     " << error <<
                        "\n tolerance: " << swapTolerance);
    }

    // check caplet fit
    for (Size i=0; i<numberOfRates; ++i) {
        error = std::fabs(capletVols[i]-capletVols_[i]);
        if (error>capletTolerance)
            FAIL_CHECK("failed to reproduce " << io::ordinal(i+1) << " caplet vol:"
                        "\n expected:         " << io::rate(capletVols_[i]) <<
                        "\n realized:         " << io::rate(capletVols[i]) <<
                        "\n percentage error: " << error/capletVols_[i] <<
                        "\n error:            " << error <<
                        "\n tolerance:        " << capletTolerance);
    }

    Size period =2;
    Size offset =0;
    std::vector<Spread> adaptedDisplacements;
    std::shared_ptr<MarketModel> adapted = std::make_shared<FwdPeriodAdapter>(flmm,period,offset,adaptedDisplacements);
   // FwdToCotSwapAdapter newSwapMM(adapted);
   // for (Size i=0; i < newSwapMM.numberOfRates(); ++i)
     //      INFO("swap MM time dependent vols: "<< i << QL_FIXED <<
       //               std::setprecision(6) << Array(newSwapMM.timeDependentVolatility(i)));






}



TEST_CASE("MarketModelSmmCapletHomoCalibration_PeriodFunction", "[MarketModelSmmCapletHomoCalibration]")
{

    INFO("Testing max homogeneity periodic caplet calibration "
                       "in a lognormal coterminal swap market model...");

    setup();

    Size numberOfRates = todaysForwards_.size();
    Size period=2;
    Size offset = numberOfRates % period;
    Size numberBigRates = numberOfRates / period;

    EvolutionDescription evolution(rateTimes_);

    std::vector<Time> bigRateTimes(numberBigRates+1);

    for (Size i=0; i <= numberBigRates; ++i)
        bigRateTimes[i] = rateTimes_[i*period+offset];

    std::shared_ptr<PiecewiseConstantCorrelation> fwdCorr =
        std::make_shared<ExponentialForwardCorrelation>(rateTimes_,
                                      longTermCorrelation_,
                                      beta_);

    std::shared_ptr<LMMCurveState> cs = std::make_shared<LMMCurveState>(rateTimes_);
    cs->setOnForwardRates(todaysForwards_);

    std::shared_ptr<PiecewiseConstantCorrelation> corr =
        std::make_shared<CotSwapFromFwdCorrelation>(fwdCorr, *cs, displacement_);

    std::vector<PiecewiseConstantAbcdVariance >
                                    swapVariances;
    for (Size i=0; i<numberBigRates; ++i) {
        swapVariances.emplace_back(
            PiecewiseConstantAbcdVariance(a_, b_, c_, d_,
                                          i, bigRateTimes));
    }

    VolatilityInterpolationSpecifierabcd varianceInterpolator(period, offset, swapVariances, // these should be associated with the long rates
                                                                   rateTimes_ // these should be associated with the shorter rates
                                                                   );


    // create calibrator
    Real caplet0Swaption1Priority = 1.0;
    if (printReport_) {
        INFO("caplet market vols: " << QL_FIXED <<
                           std::setprecision(4) << io::sequence(capletVols_));
        INFO("caplet0Swapt1Prior: " << caplet0Swaption1Priority);
    }

     // calibrate
    Natural maxUnperiodicIterations = 10;
    Real toleranceUnperiodic = 1e-5; // i.e. 1 bp
    Natural max1dIterations = 100;
    Real tolerance1d = 1e-8;
    Size maxPeriodIterations = 30;
    Real periodTolerance = 1e-5;

     std::vector<Matrix> swapPseudoRoots;
     Real deformationSize;
     Real totalSwaptionError;
     std::vector<Real>  finalScales;  //scalings used for matching
     Size iterationsDone; // number of  period iteratations done
     Real errorImprovement; // improvement in error for last iteration
     Matrix modelSwaptionVolsMatrix;

       if (printReport_) {
        INFO("numberOfFactors:    " << numberOfFactors_);
        INFO("maxUnperiodicIterations:      " << maxUnperiodicIterations);
        INFO("toleranceUnperiodic:    " << io::rate(toleranceUnperiodic));
        INFO("max1dIterations: " << max1dIterations);
        INFO("tolerance1d:     " << io::rate(tolerance1d));

       }

       /*Integer failures =*/ capletSwaptionPeriodicCalibration(
        evolution,
        corr,
        varianceInterpolator,
        capletVols_,
        cs,
        displacement_,
        caplet0Swaption1Priority,
        numberOfFactors_,
        period,
        max1dIterations,
        tolerance1d,
        maxUnperiodicIterations,
        toleranceUnperiodic,
        maxPeriodIterations,
        periodTolerance,
        deformationSize,
        totalSwaptionError, // ?
        swapPseudoRoots,  // the thing we really want the pseudo root for each time step
        finalScales,  //scalings used for matching
        iterationsDone, // number of  period iteratations done
        errorImprovement, // improvement in error for last iteration
        modelSwaptionVolsMatrix // the swaption vols calibrated to at each step of the iteration
        );


    std::shared_ptr<MarketModel> smm =
        std::make_shared<PseudoRootFacade>(swapPseudoRoots,
                         rateTimes_,
                         cs->coterminalSwapRates(),
                         std::vector<Spread>(numberOfRates, displacement_));
    std::shared_ptr<MarketModel> flmm = std::make_shared<CotSwapToFwdAdapter>(smm);
    Matrix capletTotCovariance = flmm->totalCovariance(numberOfRates-1);




    std::vector<Volatility> capletVols(numberOfRates);
    for (Size i=0; i<numberOfRates; ++i) {
        capletVols[i] = std::sqrt(capletTotCovariance[i][i]/rateTimes_[i]);
    }

    Real error;
    Real capletTolerance = 1e-4; // i.e. 1 bp

    // check caplet fit
    for (Size i=0; i<numberOfRates; ++i) {
        error = std::fabs(capletVols[i]-capletVols_[i]);
        if (error>capletTolerance)
            FAIL_CHECK("failed to reproduce " << io::ordinal(i+1) << " caplet vol:"
                        "\n expected:         " << io::rate(capletVols_[i]) <<
                        "\n realized:         " << io::rate(capletVols[i]) <<
                        "\n percentage error: " << error/capletVols_[i] <<
                        "\n error:            " << error <<
                        "\n tolerance:        " << capletTolerance);
    }



    std::vector<Spread> adaptedDisplacements(numberBigRates,displacement_);
    std::shared_ptr<MarketModel> adaptedFlmm = std::make_shared<FwdPeriodAdapter>(flmm,period,offset,adaptedDisplacements);

     std::shared_ptr<MarketModel> adaptedsmm = std::make_shared<FwdToCotSwapAdapter>(adaptedFlmm);

      // check perfect swaption fit
    Real  swapTolerance = 2e-5;

    Matrix swapTerminalCovariance(adaptedsmm->totalCovariance(adaptedsmm->numberOfSteps()-1));

    for (Size i=0; i<numberBigRates; ++i) {
        Volatility expSwaptionVol = swapVariances[i].totalVolatility(i);
        // Real cov = swapTerminalCovariance[i][i];
        Time time = adaptedsmm->evolution().rateTimes()[i];
        Volatility swaptionVol =  sqrt(swapTerminalCovariance[i][i]/time);

        error = std::fabs(swaptionVol-expSwaptionVol);
        if (error>swapTolerance)
            FAIL_CHECK("failed to reproduce " << io::ordinal(i) << " swaption vol:"
                        "\n expected:  " << io::rate(expSwaptionVol) <<
                        "\n realized:  " << io::rate(swaptionVol) <<
                        "\n error:     " << error <<
                        "\n tolerance: " << swapTolerance);
    }






}


TEST_CASE("MarketModelSmmCapletHomoCalibration_SphereCylinder", "[MarketModelSmmCapletHomoCalibration]") {

    INFO("Testing sphere-cylinder optimization...");

    {
        Real R =1.0;
        Real S =0.5;
        Real alpha=1.5;
        Real Z1=1.0/sqrt(3.0);
        Real Z2=1.0/sqrt(3.0);
        Real Z3=1.0/sqrt(3.0);

        SphereCylinderOptimizer optimizer(R, S, alpha, Z1, Z2, Z3);
        Size maxIterations=100;
        Real tolerance=1e-8;
        Real y1, y2, y3;

        optimizer.findClosest(maxIterations, tolerance, y1, y2, y3);

        Real errorTol = 1e-12;
        if ( fabs(y1-1.0) > errorTol)
            FAIL_CHECK("\n failed to reproduce y1=1: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y2-0.0) > errorTol)
            FAIL_CHECK("\n failed to reproduce y2=0: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y3-0.0) > errorTol)
            FAIL_CHECK("\n failed to reproduce y3=0: "
            << y1 << ", " <<y2 << ", "  << y3);


        optimizer.findByProjection(y1, y2, y3);

        if ( fabs(y1-1.0) > errorTol)
            FAIL_CHECK("\nfindByProjection failed to reproduce y1=1: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y2-0.0) > errorTol)
            FAIL_CHECK("\n findByProjection failed to reproduce y2=0: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y3-0.0) > errorTol)
            FAIL_CHECK("\n findByProjection failed to reproduce y3=0: "
            << y1 << ", " <<y2 << ", "  << y3);
    }

   {
        Real R =5.0;
        Real S =1.0;
        Real alpha=1.0;
        Real Z1=1.0;
        Real Z2=2.0;
        Real Z3=sqrt(20.0);

        SphereCylinderOptimizer optimizer(R, S, alpha, Z1, Z2, Z3);
        Size maxIterations=100;
        Real tolerance=1e-8;
        Real y1,y2,y3;

        optimizer.findClosest(maxIterations, tolerance, y1, y2, y3);

        Real errorTol = 1e-4;
        if ( fabs(y1-1.03306) > errorTol)
            FAIL_CHECK("\n failed to reproduce y1=1.03306: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y2-0.999453) > errorTol)
            FAIL_CHECK("\n failed to reproduce y2=0.999453: "
            << y1 << ", " << y2 << ", "  << y3);

        if ( fabs(y3-4.78893) > errorTol)
            FAIL_CHECK("\n failed to reproduce y3=4.78893: "
            << y1 << ", " <<y2 << ", "  << y3);


        optimizer.findByProjection(y1, y2, y3);

        if ( fabs(y1-1.0) > errorTol)
            FAIL_CHECK("\n findByProjection failed to reproduce y1 =1: "
            << y1 << " " << y2 << " "  << y3);

        if ( fabs(y2-1.0) > errorTol)
            FAIL_CHECK("\n findByProjection failed to reproduce y2 =1: "
            << y1 << " " << y2 << " "  << y3);

        if ( fabs(y3-sqrt(23.0)) > errorTol)
            FAIL_CHECK("\n findByProjection failed to reproduce y3 =sqrt(23): "
            << y1 << " " <<y2 << " "  << y3);

    }
}
