#include "trigger/Tee.hpp"
#include "triggeralgs/TriggerCandidate.hpp"

namespace dunedaq {
namespace trigger {

using TCTee = Tee<triggeralgs::TriggerCandidate>;

}
}

DEFINE_DUNE_DAQ_MODULE(dunedaq::trigger::TCTee)
