/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2011 Klaus Spanderen

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

/*! \file fdhullwhiteswaptionengine.cpp
*/

#include <ql/exercise.hpp>
#include <ql/indexes/iborindex.hpp>
#include <ql/processes/ornsteinuhlenbeckprocess.hpp>
#include <ql/pricingengines/swaption/fdhullwhiteswaptionengine.hpp>
#include <ql/methods/finitedifferences/solvers/fdmsolverdesc.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/fdmsimpleprocess1dmesher.hpp>
#include <ql/methods/finitedifferences/solvers/fdmhullwhitesolver.hpp>
#include <ql/methods/finitedifferences/utilities/fdmaffinemodelswapinnervalue.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>

#include <memory>
#include <algorithm>

namespace QuantLib {

    FdHullWhiteSwaptionEngine::FdHullWhiteSwaptionEngine(
        const std::shared_ptr<HullWhite>& model,
        Size tGrid, Size xGrid, 
        Size dampingSteps, Real invEps,
        const FdmSchemeDesc& schemeDesc)
    : GenericModelEngine<HullWhite, 
                         Swaption::arguments, Swaption::results>(model),
      tGrid_(tGrid),
      xGrid_(xGrid),
      dampingSteps_(dampingSteps),
      invEps_(invEps),
      schemeDesc_(schemeDesc) {
    }

    void FdHullWhiteSwaptionEngine::calculate() const {
        // 1. Term structure
        const Handle<YieldTermStructure> ts = model_->termStructure();

        // 2. Mesher
        const DayCounter dc = ts->dayCounter();
        const Date referenceDate = ts->referenceDate();
        const Time maturity = dc.yearFraction(referenceDate,
                                              arguments_.exercise->lastDate());


        const std::shared_ptr<OrnsteinUhlenbeckProcess> process =
            std::make_shared<OrnsteinUhlenbeckProcess>(model_->a(), model_->sigma());

        const std::shared_ptr<Fdm1dMesher> shortRateMesher =
            std::make_shared<FdmSimpleProcess1dMesher>(xGrid_, process, maturity,1,invEps_);

        const std::shared_ptr<FdmMesher> mesher =
            std::make_shared<FdmMesherComposite>(shortRateMesher);

        // 3. Inner Value Calculator
        const std::vector<Date>& exerciseDates = arguments_.exercise->dates();
        std::map<Time, Date> t2d;

        for (Size i=0; i < exerciseDates.size(); ++i) {
            const Time t = dc.yearFraction(referenceDate, exerciseDates[i]);
            QL_REQUIRE(t >= 0, "exercise dates must not contain past date");

            t2d[t] = exerciseDates[i];
        }

        const Handle<YieldTermStructure> disTs = model_->termStructure();
        const Handle<YieldTermStructure> fwdTs
            = arguments_.swap->iborIndex()->forwardingTermStructure();

        QL_REQUIRE(fwdTs->dayCounter() == disTs->dayCounter(),
                "day counter of forward and discount curve must match");
        QL_REQUIRE(fwdTs->referenceDate() == disTs->referenceDate(),
                "reference date of forward and discount curve must match");

        const std::shared_ptr<HullWhite> fwdModel =
            std::make_shared<HullWhite>(fwdTs, model_->a(), model_->sigma());

        const std::shared_ptr<FdmInnerValueCalculator> calculator =
             std::make_shared<FdmAffineModelSwapInnerValue<HullWhite>>(
                 model_.currentLink(), fwdModel,
                 arguments_.swap, t2d, mesher, 0);

        // 4. Step conditions
        const std::shared_ptr<FdmStepConditionComposite> conditions =
             FdmStepConditionComposite::vanillaComposite(
                 DividendSchedule(), arguments_.exercise,
                 mesher, calculator, referenceDate, dc);

        // 5. Boundary conditions
        const FdmBoundaryConditionSet boundaries;

        // 6. Solver
        FdmSolverDesc solverDesc = { mesher, boundaries, conditions,
                                     calculator, maturity,
                                     tGrid_, dampingSteps_ };

        const std::unique_ptr<FdmHullWhiteSolver> solver =
            std::make_unique<FdmHullWhiteSolver>(model_, solverDesc, schemeDesc_);

        results_.value = solver->valueAt(0.0);
    }
}
