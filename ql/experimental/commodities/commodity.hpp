/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 J. Erik Radmall

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

/*! \file commodity.hpp
    \brief Commodity base class
*/

#ifndef quantlib_commodity_hpp
#define quantlib_commodity_hpp

#include <ql/instrument.hpp>
#include <ql/money.hpp>
#include <vector>
#include <iosfwd>

namespace QuantLib {

    typedef std::map<std::string, std::any> SecondaryCosts;
    typedef std::map<std::string, Money> SecondaryCostAmounts;

    std::ostream& operator<<(std::ostream& out,
                             const SecondaryCostAmounts& secondaryCostAmounts);


    struct PricingError {
        enum Level { Info, Warning, Error, Fatal };

        Level errorLevel_;
        std::string tradeId_;
        std::string error_;
        std::string detail_;

        PricingError(Level errorLevel,
                     const std::string& error,
                     const std::string& detail)
        : errorLevel_(errorLevel), error_(error), detail_(detail) {}
    };

    typedef std::vector<PricingError> PricingErrors;

    std::ostream& operator<<(std::ostream& out, const PricingError& error);
    std::ostream& operator<<(std::ostream& out, const PricingErrors& errors);


    //! Commodity base class
    /*! \ingroup instruments */
    class Commodity : public Instrument {
      public:
        explicit Commodity(const std::shared_ptr<SecondaryCosts>& secondaryCosts);
        const std::shared_ptr<SecondaryCosts>& secondaryCosts() const;
        const SecondaryCostAmounts& secondaryCostAmounts() const;
        const PricingErrors& pricingErrors() const;
        void addPricingError(PricingError::Level errorLevel,
                             const std::string& error,
                             const std::string& detail = "") const;
      protected:
        std::shared_ptr<SecondaryCosts> secondaryCosts_;
        mutable PricingErrors pricingErrors_;
        mutable SecondaryCostAmounts secondaryCostAmounts_;
    };

}

#endif
