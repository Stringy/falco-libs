/*
Copyright (C) 2021 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#ifndef _WIN32
#include <dlfcn.h>
#include <inttypes.h>
#include <string.h>
#include <vector>
#include <sstream>
#endif
#include <json/json.h>

#include "sinsp.h"
#include "sinsp_int.h"
#include "filter.h"
#include "filterchecks.h"
#include "plugin.h"
#include "plugin_evt_processor.h"

#include <third-party/tinydir.h>

using namespace std;

extern sinsp_filter_check_list g_filterlist;

///////////////////////////////////////////////////////////////////////////////
// source_plugin filter check implementation
// This class implements a dynamic filter check that acts as a bridge to the
// plugin simplified field extraction implementations
///////////////////////////////////////////////////////////////////////////////
class sinsp_filter_check_plugin : public sinsp_filter_check
{
public:
	enum check_type
	{
		TYPE_CNT = 0,
	};

	sinsp_filter_check_plugin()
	{
		m_info.m_name = "plugin";
		m_info.m_fields = NULL;
		m_info.m_nfields = 0;
		m_info.m_flags = filter_check_info::FL_NONE;
		m_cnt = 0;
	}

	sinsp_filter_check_plugin(std::shared_ptr<sinsp_plugin> plugin)
	{
		m_plugin = plugin;
		m_info.m_name = plugin.name() + string(" (plugin)");
		m_info.m_fields = plugin.fields();
		m_info.m_nfields = plugin.nfields();
		m_info.m_flags = filter_check_info::FL_NONE;
		m_cnt = 0;
	}

	sinsp_filter_check_plugin(const sinsp_filter_check_plugin &p)
	{
		m_plugin = p.m_plugin;
		m_info = p.m_info;
	}

	virtual ~sinsp_filter_check_plugin()
	{
	}

	int32_t parse_field_name(const char* str, bool alloc_state, bool needed_for_filtering)
	{
		int32_t res = sinsp_filter_check::parse_field_name(str, alloc_state, needed_for_filtering);

		if(res != -1)
		{
			string val(str);
			size_t pos1 = val.find_first_of('[', 0);
			if(pos1 != string::npos)
			{
				size_t argstart = pos1 + 1;
				if(argstart < val.size())
				{
					m_argstr = val.substr(argstart);
					size_t pos2 = m_argstr.find_first_of(']', 0);
					m_argstr = m_argstr.substr(0, pos2);
					m_arg = (char*)m_argstr.c_str();
					return pos1 + pos2 + 2;
				}
			}
		}

		return res;
	}

	sinsp_filter_check* allocate_new()
	{
		return new sinsp_filter_check_plugin(*this);
	}

	uint8_t* extract(sinsp_evt *evt, OUT uint32_t* len, bool sanitize_strings)
	{
		//
		// Reject any event that is not generated by a plugin
		//
		if(evt->get_type() != PPME_PLUGINEVENT_E)
		{
			return NULL;
		}

		//
		// If this is a source plugin, reject events that have not generated by this
		// plugin specifically
		//
		sinsp_evt_param *parinfo;
		if(m_plugin->type() == TYPE_SOURCE_PLUGIN)
		{
			parinfo = evt->get_param(0);
			ASSERT(parinfo->m_len == sizeof(int32_t));
			uint32_t pgid = *(int32_t *)parinfo->m_val;
			if(pgid != m_id)
			{
				return NULL;
			}
		}

		//
		// If the plugin exports an async_extractor (for performance reasons) which
		// has not been configured yet, configure and initialize it here
		//
		if(!m_psource_info->is_async_extractor_configured)
		{
			if(m_psource_info->register_async_extractor)
			{
				m_psource_info->async_extractor_info.wait_ctx = new sinsp_async_extractor_ctx();
				m_psource_info->async_extractor_info.cb_wait = [](void *wait_ctx)
				{
					return static_cast<sinsp_async_extractor_ctx *>(wait_ctx)->wait();
				};

				if(m_psource_info->register_async_extractor(m_psource_info->state, &(m_psource_info->async_extractor_info)) != SCAP_SUCCESS)
				{
					throw sinsp_exception(string("error in plugin ") + m_name + ": " + m_psource_info->get_last_error(m_psource_info->state));
				}

				m_psource_info->is_async_extractor_present = true;
			}
			else
			{
				m_psource_info->is_async_extractor_present = false;
			}

			m_psource_info->is_async_extractor_configured = true;
		}

		//
		// Get the event payload
		//
		parinfo = evt->get_param(1);
		*len = 0;

		ppm_param_type type = m_info.m_fields[m_field_id].m_type;

		if(m_psource_info->is_async_extractor_present)
		{
			m_psource_info->async_extractor_info.evtnum = evt->get_num();
			m_psource_info->async_extractor_info.id = m_field_id;
			m_psource_info->async_extractor_info.ftype = type;
			m_psource_info->async_extractor_info.arg = m_arg;
			m_psource_info->async_extractor_info.data = parinfo->m_val;
			m_psource_info->async_extractor_info.datalen= parinfo->m_len;
		}

		switch(type)
		{
		case PT_CHARBUF:
		{
			if(m_psource_info->extract_str == NULL)
			{
				throw sinsp_exception(string("plugin ") + m_name + " is missing the extract_str export");
			}

			char* pret;
			if(m_psource_info->is_async_extractor_present)
			{
				static_cast<sinsp_async_extractor_ctx *>(m_psource_info->async_extractor_info.wait_ctx)->notify();
				pret = m_psource_info->async_extractor_info.res_str;

				int32_t rc = m_psource_info->async_extractor_info.rc;
				if(rc != SCAP_SUCCESS)
				{
					if(rc == SCAP_NOT_SUPPORTED)
					{
						throw sinsp_exception("plugin extract error: missing plugin_extract_string export");
					}
					else
					{
						throw sinsp_exception("plugin extract error: " + to_string(rc));
					}
				}
			}
			else
			{
				pret = m_psource_info->extract_str(
					m_psource_info->state,
					evt->get_num(),
					m_field_id,
					m_arg,
					(uint8_t *)parinfo->m_val,
					parinfo->m_len);
			}

			if(pret != NULL)
			{
				*len = strlen(pret);
			}
			else
			{
				*len = 0;
			}
			return (uint8_t*)pret;
		}
		case PT_UINT64:
		{
			if(m_psource_info->extract_u64 == NULL)
			{
				throw sinsp_exception(string("plugin ") + m_name + " is missing the extract_u64 export");
			}

			uint32_t present;
			if(m_psource_info->is_async_extractor_present)
			{
				static_cast<sinsp_async_extractor_ctx *>(m_psource_info->async_extractor_info.wait_ctx)->notify();
				present = m_psource_info->async_extractor_info.field_present;
				m_u64_res = m_psource_info->async_extractor_info.res_u64;

				int32_t rc = m_psource_info->async_extractor_info.rc;
				if(rc != SCAP_SUCCESS)
				{
					if(rc == SCAP_NOT_SUPPORTED)
					{
						throw sinsp_exception("plugin extract error: missing plugin_extract_u64 export");
					}
					else
					{
						throw sinsp_exception("plugin extract error: " + to_string(rc));
					}
				}
			}
			else
			{
				m_u64_res = m_psource_info->extract_u64(
					m_psource_info->state,
					evt->get_num(),
					m_field_id, m_arg,
					(uint8_t *)parinfo->m_val,
					parinfo->m_len,
					&present);
			}

			if(present == 0)
			{
				return NULL;
			}

			return (uint8_t*)&m_u64_res;
		}
		default:
			ASSERT(false);
			throw sinsp_exception("plugin extract error: unsupported field type " + to_string(type));
			break;
		}

		return NULL;
	}

	void set_name(string name)
	{
		m_info.m_name = name;
	}

	void set_fields(filtercheck_field_info* fields, uint32_t nfields)
	{
		m_info.m_fields = fields;
		m_info.m_nfields = nfields;
	}

	uint64_t m_cnt;
	string m_argstr;
	char* m_arg = NULL;
	uint64_t m_u64_res;

	std::shared_ptr<sinsp_plugin> m_plugin;
};

///////////////////////////////////////////////////////////////////////////////
// sinsp_plugin implementation
///////////////////////////////////////////////////////////////////////////////

void sinsp_plugin::version::version(const char *version_str)
	: m_valid(false)
{
	if(version_str)
	{
		m_valid = (sscanf(version.c_str(), "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
				  &m_version_major, &m_version_minor, &m_version_patch) == 3);
	}
}

void sinsp_plugin::version::as_string() const
{
	return std::to_string(m_version_major) + "." +
		std::to_string(m_version_minor) + "." +
		std::to_string(m_version_patch);
}

void sinsp_plugin::event::event()
	: m_data(NULL), m_datalen(0), m_ts(0)
{
}

void sinsp_plugin::event::event(uint8_t *data, uint32_t datalen, uint64_t ts)
{
	set(data, datalen, ts);
}

void sinsp_plugin::event::~event()
{
}

void sinsp_plugin::event::set(uint8_t *data, uint32_t datalen, uint64_t ts)
{
	m_data = data;
	m_datalen = datalen;
	m_ts = ts;
}

const uint8_t *sinsp_plugin::event::data()
{
	return m_data.get();
}

uint32_t sinsp_plugin::event::datalen()
{
	return m_datalen;
}

uint64_t sinsp_plugin::event::ts()
{
	return m_ts;
}

void sinsp_plugin::register_plugin(sinsp* inspector, string filepath, char* config)
{
	string errstr;
	std::shared_ptr<sinsp_plugin> plugin = create_plugin(filepath, config, errstr);

	if (!plugin)
	{
		throw sinsp_exception("cannot load plugin " + filepath + ": " + errstr.c_str());
	}

	try
	{
		inspector->add_plugin(plugin);
	}
	catch(sinsp_exception const& e)
	{
		throw sinsp_exception("cannot add plugin " + filepath + " to inspector: " + e.what());
	}
}

std::shared_ptr<sinsp_plugin> sinsp_plugin::create_plugin(string filepath, std::string &errstr)
{
	std::shared_ptr<sinsp_plugin> ret;

#ifdef _WIN32
	HINSTANCE handle = LoadLibrary(filepath.c_str());
#else
	void* handle = dlopen(filepath.c_str(), RTLD_LAZY);
#endif
	if(handle == NULL)
	{
		errstr = "error loading plugin " + filepath + ": " + strerror(errno);
		return ret;
	}

	// Before doing anything else, check the required api
	// version. If it doesn't match, return an error.
	char * (*get_required_api_version) = getsym(handle, "get_required_api_version");
	if(get_required_api_version == NULL)
	{
		errstr = "Plugin did not export get_required_api_version function";
		return ret;
	}

	char *version_str = get_required_api_version();
	api_version v(version_str);
	if(!v.valid())
	{
		errstr = "Could not parse version string from " + version_str;
		return ret;
	}

	if(!(v.m_version_major == PLUGIN_API_VERSION_MAJOR && v.m_version_minor <= PLUGIN_API_VERSION_MINOR))
	{
		errstr = "Unsupported plugin api version " + std::to_string(v.m_version_major);
		return ret;
	}

	uint32_t (*get_type) = getsym(handle, "get_type");
	if(get_type == NULL)
	{
		errstr = "Plugin did not export get_type function";
		return ret;
	}

	plugin_type = get_type();

	switch(plugin_type)
	{
	case TYPE_SOURCE_PLUGIN:
		sinsp_source_plugin *plugin = new sinsp_source_plugin();
		if(!plugin->resolve_dylib_symbols(handle, errstr))
		{
			return ret;
		}
		ret = (sinsp_plugin *) plugin;
		break;
	case TYPE_EXTRACTOR_PLUGIN:
		sinsp_extractor_plugin *plugin = new sinsp_extractor_plugin();
		if(!plugin->resolve_dylib_symbols(handle, errstr))
		{
			return ret;
		}
		ret = (sinsp_plugin *) plugin;
		break;
	}

	return ret;
}

std::string sinsp_plugin::plugin_infos(sinsp* inspector)
{
	const std::vector<std::shared_ptr<sinsp_plugin>>> &plist = inspector->get_plugins();

	std::ostringstream os;

	for(auto p : plist)
	{
		os << "Name: " << p->name() << std::endl;
		os << "Description: " << p->description() << std::endl;
		os << "Contact: " << p->contact() << std::endl;
		os << "Version: " << p->version().as_string() << std::endl;

		if(p->type() == TYPE_SOURCE_PLUGIN)
		{
			sp = static_cast<sinsp_source_plugin *> p.get();
			os << "Type: source plugin" << std::endl;
			os << "ID: " << sp->id() << std::endl;
		}
		else
		{
			os << "Type: extractor plugin" << std::endl;
		}
	}

	return os.str();
}

sinsp_plugin::sinsp_plugin(sinsp* inspector)
	: m_nfields(0)
{
	m_inspector = inspector;
}

sinsp_plugin::~sinsp_plugin()
{
	if(m_source_info.register_async_extractor)
	{
		if(m_source_info.is_async_extractor_present == true)
		{
			static_cast<sinsp_async_extractor_ctx *>(m_source_info.async_extractor_info.wait_ctx)->shutdown();
		}
	}

	destroy();
}

sinsp_plugin::init(char *config, int32_t &rc)
{
	if (!m_plugin_info.init)
	{
		return;
	}

	int32_t prc;

	m_plugin_handle = m_plugin_info.init(config, &prc);
	rc = prc;

	return (m_plugin_handle != NULL);
}

sinsp_plugin::destroy()
{
	if(m_plugin_handle && m_plugin_info.destroy)
	{
		m_plugin_info.destroy(m_plugin_handle);
		m_plugin_handle = NULL;
	}
}

std::string sinsp_plugin::get_last_error()
{
	std::string ret = str_from_alloc_charbuf(m_plugin_info.get_last_error());

	return ret;
}

const version &sinsp_plugin::name()
{
	return m_name;
}

const version &sinsp_plugin::description()
{
	return m_description;
}

const version &sinsp_plugin::contact()
{
	return m_contact;
}

const version &sinsp_plugin::plugin_version()
{
	return m_plugin_version;
}

const filtercheck_field_info *sinsp_plugin::fields()
{
	return m_fields.get();
}

int32_t sinsp_plugin::nfields()
{
	return m_nfields;
}

std::string sinsp_plugin::extract_str(uint64_t evtnum, uint32_t id, char *arg, sinsp_plugin::event &evt)
{
	std::string ret = "<NA>";

	if(!m_plugin_info.extract_str || !m_plugin_handle)
	{
		return ret;
	}

	ret = str_from_alloc_charbuf(m_plugin_info.extract_str(m_plugin_handle, evtnum, id, arg, evt.data(), evt.datalen()));

	return ret;
}

uint64_t sinsp_plugin::extract_u64(uint64_t evtnum, uint32_t id, char *arg, sinsp_plugin::event &evt, uint32_t &field_present)
{
	uint64_t ret = 0;

	if(!m_plugin_info.extract_str || !m_plugin_handle)
	{
		return ret;
	}

	uint32_t fp;

	ret = m_plugin_info.extract_u64(m_plugin_handle, evtnum, id, arg, evt.data(), evt.datalen(), &fp));

	field_present = fp;

	return ret;
}

void* sinsp_plugin::getsym(void* handle, const char* name, std::string &errstr)
{
	void *ret;

#ifdef _WIN32
	ret = GetProcAddress((HINSTANCE)handle, name);
#else
	ret = dlsym(handle, name);
#endif

	if(ret == NULL)
	{
		errstr = "Dynamic library symbol " + name + " not present";
	}

	return ret;
}

// Used below--set a std::string from the provided allocated charbuf and free() the charbuf.
std::string sinsp_plugin::str_from_alloc_charbuf(char *charbuf)
{
	std::string str;

	if(charbuf != NULL)
	{
		str = charbuf;
		free(charbuf);
	}

	return str;
}

bool sinsp_plugin::resolve_dylib_symbols(void *handle, std::string &errstr)
{
	// Some functions are required and return false if not found.
	if((m_plugin_info.get_last_error = getsym(handle, "plugin_get_last_error", errstr)) == NULL ||
	   (m_plugin_info.get_name = getsym(handle, "plugin_get_name", errstr)) == NULL ||
	   (m_plugin_info.get_description = getsym(handle, "plugin_get_description", errstr)) == NULL ||
	   (m_plugin_info.get_contact = getsym(handle, "plugin_get_contact", errstr)) == NULL ||
	   (m_plugin_info.get_version = getsym(handle, "plugin_get_version", errstr)) == NULL)
	{
		return false;
	}

	// Others are not and the values will be checked when needed.
	m_plugin_info.init = getsym(handle, "plugin_init", errstr);
	m_plugin_info.destroy = getsym(handle, "plugin_destroy", errstr);
	m_plugin_info.get_fields = getsym(handle, "plugin_get_fields", errstr);
	m_plugin_info.extract_str = getsym(handle, "plugin_extract_str", errstr);
	m_plugin_info.extract_u64 = getsym(handle, "plugin_extract_u64", errstr);
	m_plugin_info.register_async_extractor = getsym(handle, "plugin_register_async_extractor", errstr);

	m_name = str_from_alloc_charbuf(m_plugin_info.get_name());
	m_description = str_from_alloc_charbuf(m_plugin_info.get_description());
	m_contact = str_from_alloc_charbuf(m_plugin_info.get_contact());
	char *version_str = m_plugin_info.get_version();
	m_version = sinsp_plugin::version(version_str);
	if(!m_version.m_valid)
	{
		errstr = "Could not parse version string from " + version_str;
		return false;
	}

	//
	// If filter fields are exported by the plugin, get the json from get_fields(),
	// parse it, create our list of fields and feed them to a new sinsp_filter_check_plugin
	// extractor.
	//
	if(m_plugin_info.get_fields)
	{
		char* sfields = m_plugin_info.get_fields();
		if(sfields == NULL)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": get_fields returned a null string");
		}
		string json(sfields);
		SINSP_DEBUG("Parsing Fields JSON=%s", json.c_str());
		Json::Value root;
		if(Json::Reader().parse(json, root) == false || root.type() != Json::arrayValue)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": get_fields returned an invalid JSON");
		}

		filtercheck_field_info *fields = new filtercheck_field_info[root.size()];
		if(fields == NULL)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": could not allocate memory");
		}

		// Take ownership of the pointer right away so it can't be leaked.
		m_fields.reset(fields);
		m_nfields = root.size();

		for(Json::Value::ArrayIndex j = 0; j < root.size(); j++)
		{
			filtercheck_field_info &tf = m_fields.get()[j];
			tf.m_flags = EPF_NONE;

			const Json::Value &jvtype = root[j]["type"];
			string ftype = jvtype.asString();
			if(ftype == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no type");
			}
			const Json::Value &jvname = root[j]["name"];
			string fname = jvname.asString();
			if(fname == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no name");
			}
			const Json::Value &jvdesc = root[j]["desc"];
			string fdesc = jvdesc.asString();
			if(fdesc == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no desc");
			}

			strncpy(tf.m_name, fname.c_str(), sizeof(tf.m_name));
			strncpy(tf.m_description, fdesc.c_str(), sizeof(tf.m_description));
			tf.m_print_format = PF_DEC;
			if(ftype == "string")
			{
				tf.m_type = PT_CHARBUF;
			}
			else if(ftype == "uint64")
			{
				tf.m_type = PT_UINT64;
			}
			// XXX/mstemm are these actually supported?
			else if(ftype == "int64")
			{
				tf.m_type = PT_INT64;
			}
			else if(ftype == "float")
			{
				tf.m_type = PT_DOUBLE;
			}
			else
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": invalid field type " + ftype);
			}
		}

		//
		// Create and register the filter check associated to this plugin
		//
		m_filtercheck = new sinsp_filter_check_plugin(*this);

		g_filterlist.add_filter_check(m_filtercheck);

		// XXX async extractor stuff
		// if(avoid_async)
		// {
		// 	m_source_info.register_async_extractor = NULL;
		// }

		// return (m_source_info.register_async_extractor != NULL);
	}

	// XXX async extractor stuff

	return true;
}

sinsp_source_plugin::sinsp_source_plugin()
	: m_instance_handle(NULL)
{
}

virtual ~sinsp_source_plugin::sinsp_source_plugin()
{
	close();
}

uint32_t sinsp_source_plugin::id()
{
	return m_id;
}

const std::string &sinsp_source_plugin::event_source()
{
	return m_event_source;
}

bool sinsp_source_plugin::open(char *params, int32_t &rc)
{
	int32_t orc;

	m_instance_handle = m_source_plugin_info.open(params, &orc);

	rc = orc;

	return (m_instance_handle != NULL);
}

bool sinsp_source_plugin::close()
{
	if(!m_instance_handle)
	{
		return;
	}

	m_source_plugin_info.close(m_plugin_handle, m_instance_handle);
}

int32_t sinsp_source_plugin::next(sinsp_plugin::event &evt, std::string &errbuf)
{
	uint8_t *data;
	uint32_t datalen;
	uint64_t ts;

	if(!m_instance_handle)
	{
		errbuf = "Plugin not open()ed";
		return SCAP_FAILURE;
	}

	rc = m_source_plugin_info.next(m_plugin_handle, m_instance_handle, &data, &datalen, &ts);

	if(rc == SCAP_FAILURE)
	{
		errbuf = get_last_error();
		return rc;
	}

	if(rc == SCAP_SUCCESS)
	{
		evt.set(data, datalen, ts);
	}

	return rc;
}

int32_t sinsp_source_plugin::next_batch(std::vector<sinsp_plugin::event> &events, std::string &errbuf)
{
	uint32_t nevts;
	uint8_t **datav;
	uint32_t *datalenv;
	uint64_t *tsv;

	if(!m_instance_handle)
	{
		errbuf = "Plugin not open()ed";
		return SCAP_FAILURE;
	}

	rc = m_source_plugin_info.next_batch(m_plugin_handle, m_instance_handle, &nevts, &datav, &datalenv, &tsv);

	if(rc == SCAP_FAILURE)
	{
		errbuf = get_last_error();
		return rc;
	}

	if(rc == SCAP_SUCCESS)
	{
		events.clear();

		for(uint32_t i = 0; i < nevts; i++)
		{
			events.emplace_back(datav[i], datalenv[i], tsv[i]);
		}
	}

	return rc;
}

std::string sinsp_source_plugin::get_progress(uint32_t &progress_pct)
{
	std::string ret;
	progress_pct = 0;

	if(!m_source_plugin_info.get_progress || !m_instance_handle)
	{
		return ret;
	}

	uint32_t ppct;
	ret = str_from_alloc_charbuf(m_source_plugin_info.get_progress(m_plugin_handle, m_instance_handle, &ppct));

	progress_pct = ppct;

	return ret;
}

std::string sinsp_plugin::event_to_string(sinsp_plugin::event &evt)
{
	std::string ret = "<NA>";

	if (!m_source_plugin_info.event_to_string)
	{
		return ret;
	}

	ret = str_from_alloc_charbuf(m_source_plugin_info.event_to_string(m_plugin_handle, evt.data(), evt.datalen()));

	return ret;
}

bool sinsp_source_plugin::resolve_dylib_symbols(void *handle, std::string &errstr)
{
	if (!sinsp_plugin::resolve_dylib_symbols(handle, errstr))
	{
		return false;
	}

	// We resolve every symbol, even those that are not actually
	// used by this derived class, just to ensure that
	// m_source_plugin_info is complete. (The struct can be passed
	// down to libscap when reading/writing capture files).
	//
	// Some functions are required and return false if not found.
	if((m_source_plugin_info.get_required_api_version = getsym(handle, "plugin_get_required_api_version", errstr)) == NULL ||
	   (m_source_plugin_info.init = getsym(handle, "plugin_init", errstr)) == NULL ||
	   (m_source_plugin_info.destroy = getsym(handle, "plugin_destroy", errstr)) == NULL ||
	   (m_source_plugin_info.get_last_error = getsym(handle, "plugin_get_last_error", errstr)) == NULL ||
	   (m_source_plugin_info.get_type = getsym(handle, "plugin_get_type", errstr)) == NULL ||
	   (m_source_plugin_info.get_id = getsym(handle, "plugin_get_id", errstr)) == NULL ||
	   (m_source_plugin_info.get_name = getsym(handle, "plugin_get_name", errstr)) == NULL ||
	   (m_source_plugin_info.get_description = getsym(handle, "plugin_get_description", errstr)) == NULL ||
	   (m_source_plugin_info.get_contact = getsym(handle, "plugin_get_contact", errstr)) == NULL ||
	   (m_source_plugin_info.get_version = getsym(handle, "plugin_get_version", errstr)) == NULL ||
	   (m_source_plugin_info.get_event_source = getsym(handle, "plugin_get_event_source", errstr)) == NULL ||
	   (m_source_plugin_info.open = getsym(handle, "plugin_open", errstr)) == NULL ||
	   (m_source_plugin_info.close = getsym(handle, "plugin_close", errstr)) == NULL ||
	   (m_source_plugin_info.next = getsym(handle, "plugin_next", errstr)) == NULL ||
	   (m_source_plugin_info.event_to_string = getsym(handle, "plugin_event_to_string", errstr)) == NULL)
	{
		return false;
	}

	// Others are not.
	m_source_plugin_info.get_fields = getsym(handle, "plugin_get_fields", errstr);
	m_source_plugin_info.get_progress = getsym(handle, "plugin_get_progress", errstr);
	m_source_plugin_info.event_to_string = getsym(handle, "plugin_event_to_string", errstr);
	m_source_plugin_info.extract_str = getsym(handle, "plugin_extract_str", errstr);
	m_source_plugin_info.extract_u64 = getsym(handle, "plugin_extract_u64", errstr);
	m_source_plugin_info.next_batch = getsym(handle, "plugin_next_batch", errstr);
	m_source_plugin_info.register_async_extractor = getsym(handle, "plugin_register_async_extractor", errstr);

	m_id = m_source_plugin_info.get_id();
	m_event_source = str_from_alloc_charbuf(m_source_plugin_info.get_event_source());

	return true;
}

sinsp_extractor_plugin::sinsp_extractor_plugin()
{
}

sinsp_extractor_plugin::~sinsp_extractor_plugin()
{
}

const std::vector<std::string> &extract_event_sources()
{
	return m_extract_event_sources;
}

bool sinsp_extractor_plugin::resolve_dylib_symbols(void *handle, std::string &errstr)
{
	if (!sinsp_plugin::resolve_dylib_symbols(handle, errstr))
	{
		return false;
	}

	// We resolve every symbol, even those that are not actually
	// used by this derived class, just to ensure that
	// m_extractor_plugin_info is complete. (The struct can be passed
	// down to libscap when reading/writing capture files).
	//
	// Some functions are required and return false if not found.
	if((m_extractor_plugin_info.get_required_api_version = getsym(handle, "plugin_get_required_api_version", errstr)) == NULL ||
	   (m_extractor_plugin_info.init = getsym(handle, "plugin_init", errstr)) == NULL ||
	   (m_extractor_plugin_info.destroy = getsym(handle, "plugin_destroy", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_type = getsym(handle, "plugin_get_type", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_last_error = getsym(handle, "plugin_get_last_error", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_type = getsym(handle, "plugin_get_type", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_name = getsym(handle, "plugin_get_name", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_description = getsym(handle, "plugin_get_description", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_contact = getsym(handle, "plugin_get_contact", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_version = getsym(handle, "plugin_get_version", errstr)) == NULL ||
	   (m_extractor_plugin_info.get_fields = getsym(handle, "plugin_get_fields", errstr)) == NULL ||
	   (m_extractor_plugin_info.extract_str = getsym(handle, "plugin_extract_str", errstr)) == NULL ||
	   (m_extractor_plugin_info.extract_u64 = getsym(handle, "plugin_extract_u64", errstr)) == NULL)
	{
		return false;
	}

	// Others are not.
	m_extractor_plugin_info.get_extract_event_sources = getsym(handle, "plugin_get_extract_event_sources", errstr);
	m_extractor_plugin_info.next_batch = getsym(handle, "plugin_next_batch", errstr);
	m_extractor_plugin_info.register_async_extractor = getsym(handle, "plugin_register_async_extractor", errstr);

	if (m_plugin_info.get_extract_event_sources != NULL)
	{
		std::string esources = string_alloc_charbuf(m_source_info.get_extract_event_sources());

		if (esources.length() == 0)
		{
			throw sinsp_exception(string("error in plugin ") + m_desc.m_name + ": get_extract_event_sources returned an empty string");
		}

		Json::Value root;
		if(Json::Reader().parse(esources, root) == false || root.type() != Json::arrayValue)
		{
			throw sinsp_exception(string("error in plugin ") + m_desc.m_name + ": get_extract_event_sources did not return a json array");
		}

		for(Json::Value::ArrayIndex j = 0; j < root.size(); j++)
		{
			if(! root[j].isConvertibleTo(Json::stringValue))
			{
				throw sinsp_exception(string("error in plugin ") + m_desc.m_name + ": get_extract_event_sources did not return a json array");
			}

			m_desc.m_extract_event_sources.push_back(root[j].asString());
		}
	}

	return true;
}


