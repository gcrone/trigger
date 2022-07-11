local moo = import "moo.jsonnet";
local ns = "dunedaq.trigger.tpsetbuffercreator";
local s = moo.oschema.schema(ns);

local types = {
    size: s.number("Size", dtype="i8"),

    element_id : s.number("element_id", "u4"),

    conf: s.record("Conf", [

      s.field("tpset_buffer_size", self.size, 100,
        doc="Maximum number of TPSet that buffer will store. If maximum reached, oldest is deleted to give room for new entry (circular buffer)"),


      s.field("element", self.element_id, doc="GeoID element for sent fragments"),

    ], doc="TPSetBufferManager configuration parameters"),

};

moo.oschema.sort_select(types, ns)