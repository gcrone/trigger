local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.moduleleveltrigger";
local s = moo.oschema.schema(ns);

local types = {
  element_id : s.number("element_id_t", "u4"),
  subsystem : s.string("subsystem_t"),
  connection_name : s.string("connection_name"),
  hsi_tt_pt : s.boolean("hsi_tt_pt"),
  td_out_of_timeout_b : s.boolean("td_out_of_timeout_b"),
  candidate_type_t : s.number("candidate_type_t", "u4", doc="Candidate type"),
  time_t : s.number("time_t", "i8", doc="Time"),

  sourceid : s.record("SourceID", [
      s.field("element", self.element_id, doc="" ),
      s.field("subsystem", self.subsystem, doc="" )],
      doc="SourceID"),

  linkvec : s.sequence("link_vec", self.sourceid),
  
  conf : s.record("ConfParams", [
    s.field("links", self.linkvec,
      doc="List of link identifiers that may be included into trigger decision"),
      s.field("dfo_connection", self.connection_name, doc="Connection name to use for sending TDs to DFO"),
      s.field("dfo_busy_connection", self.connection_name, doc="Connection name to use for receiving inhibits from DFO"),
      s.field("hsi_trigger_type_passthrough", self.hsi_tt_pt, doc="Option to override the trigger type inside MLT"),
      s.field("td_out_of_timeout", self.td_out_of_timeout_b, doc="Option to drop TD if TC comes out of timeout window"),
      s.field("buffer_timeout", self.time_t, 100, doc="Buffering timeout [ms] for new TCs"),
      s.field("td_readout_limit", self.time_t, 1000, doc="Time limit [ms] for the length of TD readout window"),
  ], doc="ModuleLevelTrigger configuration parameters"),
  
};

moo.oschema.sort_select(types, ns)
