#include "storm/solver/Multiplier.h"

#include "storm-config.h"

#include "storm/storage/SparseMatrix.h"

#include "storm/adapters/RationalNumberAdapter.h"
#include "storm/adapters/RationalFunctionAdapter.h"

#include "storm/utility/macros.h"
#include "storm/solver/SolverSelectionOptions.h"
#include "storm/solver/NativeMultiplier.h"
#include "storm/solver/GmmxxMultiplier.h"
#include "storm/environment/solver/MultiplierEnvironment.h"
#include "storm/exceptions/IllegalArgumentException.h"

namespace storm {
    namespace solver {
        
        template<typename ValueType>
        Multiplier<ValueType>::Multiplier(storm::storage::SparseMatrix<ValueType> const& matrix) : matrix(matrix) {
            // Intentionally left empty.
        }
    
        template<typename ValueType>
        void Multiplier<ValueType>::clearCache() const {
            cachedVector.reset();
        }
        
        template<typename ValueType>
        void Multiplier<ValueType>::multiplyAndReduce(Environment const& env, OptimizationDirection const& dir, std::vector<ValueType> const& x, std::vector<ValueType> const* b, std::vector<ValueType>& result, std::vector<uint_fast64_t>* choices) const {
            multiplyAndReduce(env, dir, this->matrix.getRowGroupIndices(), x, b, result, choices);
        }

        template<typename ValueType>
        void Multiplier<ValueType>::multiplyAndReduceGaussSeidel(Environment const& env, OptimizationDirection const& dir, std::vector<ValueType>& x, std::vector<ValueType> const* b, std::vector<uint_fast64_t>* choices) const {
            multiplyAndReduceGaussSeidel(env, dir, this->matrix.getRowGroupIndices(), x, b, choices);
        }
    
        template<typename ValueType>
        void Multiplier<ValueType>::repeatedMultiply(Environment const& env, std::vector<ValueType>& x, std::vector<ValueType> const* b, uint64_t n) const {
            for (uint64_t i = 0; i < n; ++i) {
                multiply(env, x, b, x);
            }
        }
    
        template<typename ValueType>
        void Multiplier<ValueType>::repeatedMultiplyAndReduce(Environment const& env, OptimizationDirection const& dir, std::vector<ValueType>& x, std::vector<ValueType> const* b, uint64_t n) const {
            for (uint64_t i = 0; i < n; ++i) {
                multiplyAndReduce(env, dir, x, b, x);
            }
        }
    
        template<typename ValueType>
        void Multiplier<ValueType>::multiplyRow2(uint64_t const& rowIndex, std::vector<ValueType> const& x1, ValueType& val1, std::vector<ValueType> const& x2, ValueType& val2) const {
            multiplyRow(rowIndex, x1, val1);
            multiplyRow(rowIndex, x2, val2);
        }
        
        template<typename ValueType>
        std::unique_ptr<Multiplier<ValueType>> MultiplierFactory<ValueType>::create(Environment const& env, storm::storage::SparseMatrix<ValueType> const& matrix) {
            auto type = env.solver().multiplier().getType();
            
            // Adjust the multiplier type if an eqsolver was specified but not a multiplier
            if (!env.solver().isLinearEquationSolverTypeSetFromDefaultValue() && env.solver().multiplier().isTypeSetFromDefault()) {
                bool changed = false;
                if (env.solver().getLinearEquationSolverType() == EquationSolverType::Gmmxx && type != MultiplierType::Gmmxx) {
                    type = MultiplierType::Gmmxx;
                    changed = true;
                } else if (env.solver().getLinearEquationSolverType() == EquationSolverType::Native && type != MultiplierType::Native) {
                    type = MultiplierType::Native;
                    changed = true;
                }
                STORM_LOG_INFO_COND(!changed, "Selecting '" + toString(type) + "' as the multiplier type to match the selected equation solver. If you want to override this, please explicitly specify a different multiplier type.");
            }
            
            switch (type) {
                case MultiplierType::Gmmxx:
                    return std::make_unique<GmmxxMultiplier<ValueType>>(matrix);
                case MultiplierType::Native:
                    return std::make_unique<NativeMultiplier<ValueType>>(matrix);
            }
            STORM_LOG_THROW(false, storm::exceptions::IllegalArgumentException, "Unknown MultiplierType");
        }
        
        template class Multiplier<double>;
        template class MultiplierFactory<double>;
        
#ifdef STORM_HAVE_CARL
        template class Multiplier<storm::RationalNumber>;
        template class MultiplierFactory<storm::RationalNumber>;
        template class Multiplier<storm::RationalFunction>;
        template class MultiplierFactory<storm::RationalFunction>;
#endif
        
    }
}
