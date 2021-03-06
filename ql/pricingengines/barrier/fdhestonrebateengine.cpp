/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 Andreas Gaida
 Copyright (C) 2008, 2009 Ralph Schreyer
 Copyright (C) 2008, 2009 Klaus Spanderen

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

#include <ql/pricingengines/barrier/fdhestonrebateengine.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/methods/finitedifferences/meshers/fdmhestonvariancemesher.hpp>
#include <ql/methods/finitedifferences/utilities/fdmdirichletboundary.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmesher.hpp>

namespace QuantLib {

    FdHestonRebateEngine::FdHestonRebateEngine(
            const std::shared_ptr<HestonModel>& model,
            Size tGrid, Size xGrid, Size vGrid, Size dampingSteps,
            const FdmSchemeDesc& schemeDesc,
            const std::shared_ptr<LocalVolTermStructure>& leverageFct)
    : GenericModelEngine<HestonModel,
                        DividendBarrierOption::arguments,
                        DividendBarrierOption::results>(model),
      tGrid_(tGrid), xGrid_(xGrid), vGrid_(vGrid), 
      dampingSteps_(dampingSteps),
      schemeDesc_(schemeDesc),
      leverageFct_(leverageFct) {
    }

    void FdHestonRebateEngine::calculate() const {

        // 1. Mesher
        const std::shared_ptr<HestonProcess>& process = model_->process();
        const Time maturity = process->time(arguments_.exercise->lastDate());

        // 1.1 The variance mesher
        const Size tGridMin = 5;
        const std::shared_ptr<FdmHestonVarianceMesher> varianceMesher =
            std::make_shared<FdmHestonVarianceMesher>(vGrid_, process, maturity,
                                        std::max(tGridMin, tGrid_/50));

        // 1.2 The equity mesher
        const std::shared_ptr<StrikedTypePayoff> payoff =
            std::dynamic_pointer_cast<StrikedTypePayoff>(arguments_.payoff);

        Real xMin=Null<Real>();
        Real xMax=Null<Real>();
        if (   arguments_.barrierType == Barrier::DownIn
            || arguments_.barrierType == Barrier::DownOut) {
            xMin = std::log(arguments_.barrier);
        }
        if (   arguments_.barrierType == Barrier::UpIn
            || arguments_.barrierType == Barrier::UpOut) {
            xMax = std::log(arguments_.barrier);
        }

        const std::shared_ptr<Fdm1dMesher> equityMesher =
            std::make_shared<FdmBlackScholesMesher>(
                xGrid_,
                FdmBlackScholesMesher::processHelper(
                    process->s0(), process->dividendYield(), 
                    process->riskFreeRate(), varianceMesher->volaEstimate()),
                maturity, payoff->strike(), xMin, xMax);
        
        const std::shared_ptr<FdmMesher> mesher =
            std::make_shared<FdmMesherComposite>(equityMesher, varianceMesher);

        // 2. Calculator
        const std::shared_ptr<StrikedTypePayoff> rebatePayoff =
                std::make_shared<CashOrNothingPayoff>(Option::Call, 0.0, arguments_.rebate);
        const std::shared_ptr<FdmInnerValueCalculator> calculator =
                                std::make_shared<FdmLogInnerValue>(rebatePayoff, mesher, 0);

        // 3. Step conditions
        QL_REQUIRE(arguments_.exercise->type() == Exercise::European,
                   "only european style option are supported");

        const std::shared_ptr<FdmStepConditionComposite> conditions =
             FdmStepConditionComposite::vanillaComposite(
                                 arguments_.cashFlow, arguments_.exercise, 
                                 mesher, calculator, 
                                 process->riskFreeRate()->referenceDate(),
                                 process->riskFreeRate()->dayCounter());

        // 4. Boundary conditions
        FdmBoundaryConditionSet boundaries;
        if (   arguments_.barrierType == Barrier::DownIn
            || arguments_.barrierType == Barrier::DownOut) {
            boundaries.emplace_back(std::make_shared<FdmDirichletBoundary>(mesher, arguments_.rebate, 0,
                                         FdmDirichletBoundary::Lower));

        }
        if (   arguments_.barrierType == Barrier::UpIn
            || arguments_.barrierType == Barrier::UpOut) {
            boundaries.emplace_back(std::make_shared<FdmDirichletBoundary>(mesher, arguments_.rebate, 0,
                                         FdmDirichletBoundary::Upper));
        }

        // 5. Solver
        FdmSolverDesc solverDesc = { mesher, boundaries, conditions,
                                     calculator, maturity,
                                     tGrid_, dampingSteps_ };

        std::shared_ptr<FdmHestonSolver> solver = std::make_shared<FdmHestonSolver>(
                    Handle<HestonProcess>(process), solverDesc, schemeDesc_,
                    Handle<FdmQuantoHelper>(), leverageFct_);

        const Real spot = process->s0()->value();
        results_.value = solver->valueAt(spot, process->v0());
        results_.delta = solver->deltaAt(spot, process->v0());
        results_.gamma = solver->gammaAt(spot, process->v0());
        results_.theta = solver->thetaAt(spot, process->v0());
    }
}
