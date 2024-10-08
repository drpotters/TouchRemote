#pragma once
//! Service used to access various external (online) track info lookup services, such as freedb, to update file tags with info retrieved from those services.
class NOVTABLE info_lookup_handler : public service_base {
public:
	enum {
		flag_album_lookup = 1 << 0,
		flag_track_lookup = 1 << 1,
		//! \since 2.2: supports lookup_noninteractive() call; before 2.2, lookup_noninteractive was assumed supported if info_lookup_handler_v2 was implemented.
		flag_noninteractive = 1 << 2,
	};

	//! Retrieves human-readable name of the lookup handler to display in user interface.
	virtual void get_name(pfc::string_base & p_out) = 0;

	//! Returns one or more of flag_track_lookup, flag_album_lookup, flag_noninteractive.
	virtual t_uint32 get_flags() = 0; 

	virtual fb2k::hicon_t get_icon(int p_width, int p_height) = 0;

	//! Performs a lookup. Creates a modeless dialog and returns immediately.
	//! @param items Items to look up.
	//! @param notify Callback to notify caller when the operation has completed. Call on_completion with status code 0 to signal failure/abort, or with code 1 to signal success / new infos in metadb.
	//! @param parent Parent window for the lookup dialog. Caller will typically disable the window while lookup is in progress and enable it back when completion is signaled.
	virtual void lookup(metadb_handle_list_cref items,completion_notify::ptr notify,fb2k::hwnd_t parent) = 0;
 
	FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(info_lookup_handler);
};


class NOVTABLE info_lookup_handler_v2 : public info_lookup_handler {
	FB2K_MAKE_SERVICE_INTERFACE(info_lookup_handler_v2, info_lookup_handler);
public:
	virtual double merit() {return 0;}
	virtual void lookup_noninteractive(metadb_handle_list_cref items, completion_notify::ptr notify, fb2k::hwnd_t parent) = 0;
};

//! Since 2.2
class NOVTABLE info_lookup_handler_v3 : public info_lookup_handler_v2 {
	FB2K_MAKE_SERVICE_INTERFACE(info_lookup_handler_v3, info_lookup_handler_v2);
public:
	//! Some handlers depend on user settings to access multiple actual online services. \n
	//! Use this method to retrieve individual handlers for specific services, with proper name, icon, etc.
	//! @returns Array of info_lookup_handler objects, null if there are no subhandlers and this object should be used.
	virtual fb2k::arrayRef subhandlers() { return nullptr; }
};