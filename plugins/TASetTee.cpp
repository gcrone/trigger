#include "trigger/Tee.hpp"
#include "trigger/TASet.hpp"

namespace dunedaq {
namespace trigger {

using TASetTee = Tee<TASet>;

}
}

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TASetTee)
