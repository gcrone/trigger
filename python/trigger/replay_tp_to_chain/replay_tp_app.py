# Set moo schema search path
from dunedaq.env import get_moo_model_path
import moo.io
moo.io.default_load_path = get_moo_model_path()

from pprint import pprint
# Load configuration types
import moo.otypes

moo.otypes.load_types('trigger/triggerprimitivemaker.jsonnet')

# Import new types
import dunedaq.trigger.triggerprimitivemaker as tpm

from daqconf.core.app import App, ModuleGraph
from daqconf.core.daqmodule import DAQModule
from daqconf.core.conf_utils import Direction

# Trying to fix source ID issues for replay app
from daqconf.core.sourceid import *

def get_replay_app(INPUT_FILES: [str],
                   SLOWDOWN_FACTOR: float,
                   NUMBER_OF_LOOPS: int):

    clock_frequency_hz = 50_000_000 / SLOWDOWN_FACTOR
    modules = []

    n_streams = len(INPUT_FILES)

    tp_streams = [tpm.TPStream(filename=input_file,
                             #  region_id = 0,
                               element_id = istream,
                               output_sink_name = f"output{istream}")
                  for istream,input_file in enumerate(INPUT_FILES)]

    # tpm_connections = { f"output{istream}" : Connection(f"chan_filter{istream}.tpset_source")
    #                     for istream in range(n_streams) }
    modules.append(DAQModule(name = "tpm",
                             plugin = "TriggerPrimitiveMaker",
                             conf = tpm.ConfParams(tp_streams = tp_streams,
                                                   number_of_loops=NUMBER_OF_LOOPS,
                                                   tpset_time_offset=0,
                                                   tpset_time_width=10000,
                                                   clock_frequency_hz=clock_frequency_hz,
                                                   maximum_wait_time_us=1000,)))

    mgraph = ModuleGraph(modules)
    for istream in range(n_streams):
        mgraph.add_endpoint(f"tpsets_ru{istream}_link0", f"tpm.output{istream}", Direction.OUT, topic=["TPSets"])

    return App(modulegraph=mgraph, host="localhost", name="ReplayApp")

