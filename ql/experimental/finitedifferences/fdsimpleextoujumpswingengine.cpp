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

/*! \file fdsimpleextoujumpswingengine.cpp
    \brief Finite Differences engine for simple swing options
*/


#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/experimental/processes/extouwithjumpsprocess.hpp>
#include <ql/experimental/processes/extendedornsteinuhlenbeckprocess.hpp>
#include <ql/methods/finitedifferences/meshers/exponentialjump1dmesher.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/meshers/fdmsimpleprocess1dmesher.hpp>
#include <ql/experimental/finitedifferences/fdmextoujumpmodelinnervalue.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/fdmblackscholesmesher.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmsimpleswingcondition.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/solvers/fdm3dimsolver.hpp>
#include <ql/experimental/finitedifferences/fdmsimple3dextoujumpsolver.hpp>
#include <ql/experimental/finitedifferences/fdsimpleextoujumpswingengine.hpp>
#include <ql/methods/finitedifferences/meshers/uniform1dmesher.hpp>

namespace QuantLib {

    FdSimpleExtOUJumpSwingEngine::FdSimpleExtOUJumpSwingEngine(
              const std::shared_ptr<ExtOUWithJumpsProcess>& process,
              const std::shared_ptr<YieldTermStructure>& rTS,
              Size tGrid, Size xGrid, Size yGrid,
              const std::shared_ptr<Shape>& shape,
              const FdmSchemeDesc& schemeDesc)
    : process_(process),
      rTS_(rTS),
      shape_(shape),
      tGrid_(tGrid), xGrid_(xGrid), yGrid_(yGrid),
      schemeDesc_(schemeDesc) {
    }

    void FdSimpleExtOUJumpSwingEngine::calculate() const {

        // 1. Exercise
        std::shared_ptr<SwingExercise> swingExercise(
            std::dynamic_pointer_cast<SwingExercise>(arguments_.exercise));

        QL_REQUIRE(swingExercise, "Swing exercise supported only");

        // 2. Mesher
        const std::vector<Time> exerciseTimes
            = swingExercise->exerciseTimes(rTS_->dayCounter(),
                                           rTS_->referenceDate());

        const Time maturity = exerciseTimes.back();
        const std::shared_ptr<StochasticProcess1D> ouProcess(
                              process_->getExtendedOrnsteinUhlenbeckProcess());
        const std::shared_ptr<Fdm1dMesher> xMesher =
                     std::make_shared<FdmSimpleProcess1dMesher>(xGrid_, ouProcess,maturity);

        const std::shared_ptr<Fdm1dMesher> yMesher =
                        std::make_shared<ExponentialJump1dMesher>(yGrid_,
                                                    process_->beta(),
                                                    process_->jumpIntensity(),
                                                    process_->eta());
        const std::shared_ptr<Fdm1dMesher> exerciseMesher =
                       std::make_shared<Uniform1dMesher>(
                           0, static_cast<Real>(arguments_.maxExerciseRights),
                           arguments_.maxExerciseRights+1);

        const std::shared_ptr<FdmMesher> mesher =
            std::make_shared<FdmMesherComposite>(xMesher, yMesher, exerciseMesher);

        // 3. Calculator
        std::shared_ptr<FdmInnerValueCalculator> calculator =
                                                    std::make_shared<FdmZeroInnerValue>();
        // 4. Step conditions
        std::list<std::shared_ptr<StepCondition<Array> > > stepConditions;
        std::list<std::vector<Time> > stoppingTimes;

        // 4.1 Bermudan step conditions
        stoppingTimes.emplace_back(exerciseTimes);

        std::shared_ptr<FdmInnerValueCalculator> exerciseCalculator =
            std::make_shared<FdmExtOUJumpModelInnerValue>(arguments_.payoff, mesher, shape_);

        stepConditions.emplace_back(std::make_shared<FdmSimpleSwingCondition>(
                exerciseTimes, mesher, exerciseCalculator,
                2, arguments_.minExerciseRights));

        std::shared_ptr<FdmStepConditionComposite> conditions =
                std::make_shared<FdmStepConditionComposite>(stoppingTimes, stepConditions);


        // 5. Boundary conditions
        const FdmBoundaryConditionSet boundaries;

        // 6. set-up solver
        FdmSolverDesc solverDesc = { mesher, boundaries, conditions,
                                     calculator, maturity, tGrid_, 0 };

        const std::shared_ptr<FdmSimple3dExtOUJumpSolver> solver =
            std::make_shared<FdmSimple3dExtOUJumpSolver>(
                                    Handle<ExtOUWithJumpsProcess>(process_),
                                    rTS_, solverDesc, schemeDesc_);

        const Real x = process_->initialValues()[0];
        const Real y = process_->initialValues()[1];

        results_.value = solver->valueAt(x, y, 0.0);
    }
}
