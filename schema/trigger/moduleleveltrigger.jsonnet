local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.moduleleveltrigger";
local s = moo.oschema.schema(ns);

local types = {
  element_id : s.number("element_id_t", "u4"),
  subsystem : s.string("subsystem_t"),
  connection_name : s.string("connection_name"),
  hsi_tt_pt : s.boolean("hsi_tt_pt"),

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

  ], doc="ModuleLevelTrigger configuration parameters"),

  
};

moo.oschema.sort_select(types, ns)
