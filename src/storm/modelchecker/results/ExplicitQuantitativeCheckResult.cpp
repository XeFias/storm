#include "storm/modelchecker/results/ExplicitQuantitativeCheckResult.h"

#include "storm/modelchecker/results/ExplicitQualitativeCheckResult.h"
#include "storm/storage/BitVector.h"
#include "storm/utility/macros.h"
#include "storm/utility/constants.h"
#include "storm/utility/vector.h"
#include "storm/exceptions/InvalidOperationException.h"
#include "storm/exceptions/InvalidAccessException.h"
#include "storm/adapters/CarlAdapter.h"


namespace storm {
    namespace modelchecker {
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult() : values(map_type()) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult(map_type const& values) : values(values) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult(map_type&& values) : values(std::move(values)) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult(storm::storage::sparse::state_type const& state, ValueType const& value) : values(map_type()) {
            boost::get<map_type>(values).emplace(state, value);
        }
        
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult(vector_type const& values) : values(values) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        ExplicitQuantitativeCheckResult<ValueType>::ExplicitQuantitativeCheckResult(vector_type&& values) : values(std::move(values)) {
            // Intentionally left empty.
        }
        
        template<typename ValueType>
        typename ExplicitQuantitativeCheckResult<ValueType>::vector_type const& ExplicitQuantitativeCheckResult<ValueType>::getValueVector() const {
            return boost::get<vector_type>(values);
        }
        
        template<typename ValueType>
        typename ExplicitQuantitativeCheckResult<ValueType>::map_type const& ExplicitQuantitativeCheckResult<ValueType>::getValueMap() const {
            return boost::get<map_type>(values);
        }
        
        template<typename ValueType>
        void ExplicitQuantitativeCheckResult<ValueType>::filter(QualitativeCheckResult const& filter) {
            STORM_LOG_THROW(filter.isExplicitQualitativeCheckResult(), storm::exceptions::InvalidOperationException, "Cannot filter explicit check result with non-explicit filter.");
            STORM_LOG_THROW(filter.isResultForAllStates(), storm::exceptions::InvalidOperationException, "Cannot filter check result with non-complete filter.");
            ExplicitQualitativeCheckResult const& explicitFilter = filter.asExplicitQualitativeCheckResult();
            ExplicitQualitativeCheckResult::vector_type const& filterTruthValues = explicitFilter.getTruthValuesVector();

            if (this->isResultForAllStates()) {
                map_type newMap;
                for (auto const& element : filterTruthValues) {
                    STORM_LOG_THROW(element < this->getValueVector().size(), storm::exceptions::InvalidAccessException, "Invalid index in results.");
                    newMap.emplace(element, this->getValueVector()[element]);
                }
                this->values = newMap;
            } else {
                map_type const& map = boost::get<map_type>(values);
                
                map_type newMap;
                for (auto const& element : map) {
                    if (filterTruthValues.get(element.first)) {
                        newMap.insert(element);
                    }
                }
                
                STORM_LOG_THROW(newMap.size() == filterTruthValues.getNumberOfSetBits(), storm::exceptions::InvalidOperationException, "The check result fails to contain some results referred to by the filter.");
                
                this->values = newMap;
            }
        }
        
        template<typename ValueType>
        ValueType ExplicitQuantitativeCheckResult<ValueType>::getMin() const {
            STORM_LOG_THROW(!values.empty(),storm::exceptions::InvalidOperationException, "Minimum of empty set is not defined");
            
            if (this->isResultForAllStates()) {
                return storm::utility::minimum(boost::get<vector_type>(values));
            } else {
                return storm::utility::minimum(boost::get<map_type>(values));
            }
        }
        
        
        template<typename ValueType>
        ValueType ExplicitQuantitativeCheckResult<ValueType>::getMax() const {
            STORM_LOG_THROW(!values.empty(),storm::exceptions::InvalidOperationException, "Minimum of empty set is not defined");
            
            if (this->isResultForAllStates()) {
                return storm::utility::maximum(boost::get<vector_type>(values));
            } else {
                return storm::utility::maximum(boost::get<map_type>(values));
            }
        }
        
        template<typename ValueType>
        ValueType ExplicitQuantitativeCheckResult<ValueType>::sum() const {
            STORM_LOG_THROW(!values.empty(),storm::exceptions::InvalidOperationException, "Minimum of empty set is not defined");
            
            ValueType sum = storm::utility::zero<ValueType>();
            if (this->isResultForAllStates()) {
                for (auto& element : boost::get<vector_type>(values)) {
                    sum += element;
                }
            } else {
                for (auto& element : boost::get<map_type>(values)) {
                    sum += element.second;
                }
            }
            return sum;
        }
        
        template<typename ValueType>
        ValueType ExplicitQuantitativeCheckResult<ValueType>::average() const {
            STORM_LOG_THROW(!values.empty(),storm::exceptions::InvalidOperationException, "Minimum of empty set is not defined");
            
            ValueType sum = storm::utility::zero<ValueType>();
            if (this->isResultForAllStates()) {
                for (auto& element : boost::get<vector_type>(values)) {
                    sum += element;
                }
                return sum / boost::get<vector_type>(values).size();
            } else {
                for (auto& element : boost::get<map_type>(values)) {
                    sum += element.second;
                }
                return sum / boost::get<map_type>(values).size();
            }
        }
        
        template<typename ValueType>
        bool ExplicitQuantitativeCheckResult<ValueType>::hasScheduler() const {
            return static_cast<bool>(scheduler);
        }
        
        template<typename ValueType>
        void ExplicitQuantitativeCheckResult<ValueType>::setScheduler(std::unique_ptr<storm::storage::Scheduler>&& scheduler) {
            this->scheduler = std::move(scheduler);
        }
        
        template<typename ValueType>
        storm::storage::Scheduler const& ExplicitQuantitativeCheckResult<ValueType>::getScheduler() const {
            STORM_LOG_THROW(this->hasScheduler(), storm::exceptions::InvalidOperationException, "Unable to retrieve non-existing scheduler.");
            return *scheduler.get();
        }
        
        template<typename ValueType>
        std::ostream& ExplicitQuantitativeCheckResult<ValueType>::writeToStream(std::ostream& out) const {
            out << "[";
            if (this->isResultForAllStates()) {
                vector_type const& valuesAsVector = boost::get<vector_type>(values);
                bool first = true;
                for (auto const& element : valuesAsVector) {
                    if (!first) {
                        out << ", ";
                    } else {
                        first = false;
                    }
                    out << element;
                }
            } else {
                map_type const& valuesAsMap = boost::get<map_type>(values);
                bool first = true;
                for (auto const& element : valuesAsMap) {
                    if (!first) {
                        out << ", ";
                    } else {
                        first = false;
                    }
                    out << element.second;
                }
            }
            out << "]";
            return out;
        }
        
        template<typename ValueType>
        std::unique_ptr<CheckResult> ExplicitQuantitativeCheckResult<ValueType>::compareAgainstBound(storm::logic::ComparisonType comparisonType, ValueType const& bound) const {
            if (this->isResultForAllStates()) {
                vector_type const& valuesAsVector = boost::get<vector_type>(values);
                storm::storage::BitVector result(valuesAsVector.size());
                switch (comparisonType) {
                    case logic::ComparisonType::Less:
                        for (uint_fast64_t index = 0; index < valuesAsVector.size(); ++index) {
                            if (valuesAsVector[index] < bound) {
                                result.set(index);
                            }
                        }
                        break;
                    case logic::ComparisonType::LessEqual:
                        for (uint_fast64_t index = 0; index < valuesAsVector.size(); ++index) {
                            if (valuesAsVector[index] <= bound) {
                                result.set(index);
                            }
                        }
                        break;
                    case logic::ComparisonType::Greater:
                        for (uint_fast64_t index = 0; index < valuesAsVector.size(); ++index) {
                            if (valuesAsVector[index] > bound) {
                                result.set(index);
                            }
                        }
                        break;
                    case logic::ComparisonType::GreaterEqual:
                        for (uint_fast64_t index = 0; index < valuesAsVector.size(); ++index) {
                            if (valuesAsVector[index] >= bound) {
                                result.set(index);
                            }
                        }
                        break;
                }
                return std::unique_ptr<CheckResult>(new ExplicitQualitativeCheckResult(std::move(result)));
            } else {
                map_type const& valuesAsMap = boost::get<map_type>(values);
                std::map<storm::storage::sparse::state_type, bool> result;
                switch (comparisonType) {
                    case logic::ComparisonType::Less:
                        for (auto const& element : valuesAsMap) {
                            result[element.first] = element.second < bound;
                        }
                        break;
                    case logic::ComparisonType::LessEqual:
                        for (auto const& element : valuesAsMap) {
                            result[element.first] = element.second <= bound;
                        }
                        break;
                    case logic::ComparisonType::Greater:
                        for (auto const& element : valuesAsMap) {
                            result[element.first] = element.second > bound;
                        }
                        break;
                    case logic::ComparisonType::GreaterEqual:
                        for (auto const& element : valuesAsMap) {
                            result[element.first] = element.second >= bound;
                        }
                        break;
                }
                return std::unique_ptr<CheckResult>(new ExplicitQualitativeCheckResult(std::move(result)));
            }
        }
        
#ifdef STORM_HAVE_CARL
        template<>
        std::unique_ptr<CheckResult> ExplicitQuantitativeCheckResult<storm::RationalFunction>::compareAgainstBound(storm::logic::ComparisonType comparisonType, storm::RationalFunction const& bound) const {
            // Since it is not possible to compare rational functions against bounds, we simply call the base class method.
            return QuantitativeCheckResult::compareAgainstBound(comparisonType, bound);
        }
#endif
        
        template<typename ValueType>
        ValueType& ExplicitQuantitativeCheckResult<ValueType>::operator[](storm::storage::sparse::state_type state) {
            if (this->isResultForAllStates()) {
                return boost::get<vector_type>(values)[state];
            } else {
                return boost::get<map_type>(values)[state];
            }
        }
        
        template<typename ValueType>
        ValueType const& ExplicitQuantitativeCheckResult<ValueType>::operator[](storm::storage::sparse::state_type state) const {
            if (this->isResultForAllStates()) {
                return boost::get<vector_type>(values)[state];
            } else {
                map_type const& valuesAsMap = boost::get<map_type>(values);
                auto const& keyValuePair = valuesAsMap.find(state);
                STORM_LOG_THROW(keyValuePair != valuesAsMap.end(), storm::exceptions::InvalidOperationException, "Unknown key '" << state << "'.");
                return keyValuePair->second;
            }
        }
        
        template<typename ValueType>
        bool ExplicitQuantitativeCheckResult<ValueType>::isExplicit() const {
            return true;
        }
        
        template<typename ValueType>
        bool ExplicitQuantitativeCheckResult<ValueType>::isResultForAllStates() const {
            return values.which() == 0;
        }
        
        template<typename ValueType>
        bool ExplicitQuantitativeCheckResult<ValueType>::isExplicitQuantitativeCheckResult() const {
            return true;
        }
        
        template<typename ValueType>
        void ExplicitQuantitativeCheckResult<ValueType>::oneMinus() {
            if (this->isResultForAllStates()) {
                for (auto& element : boost::get<vector_type>(values)) {
                    element = storm::utility::one<ValueType>() - element;
                }
            } else {
                for (auto& element : boost::get<map_type>(values)) {
                    element.second = storm::utility::one<ValueType>() - element.second;
                }
            }
        }
        
        template class ExplicitQuantitativeCheckResult<double>;

#ifdef STORM_HAVE_CARL
        template class ExplicitQuantitativeCheckResult<storm::RationalNumber>;
        template class ExplicitQuantitativeCheckResult<storm::RationalFunction>;
#endif
    }
}