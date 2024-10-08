#include "foobar2000-sdk-pch.h"
#include "menu_helpers.h"

bool mainmenu_commands::g_execute_dynamic(const GUID & p_guid, const GUID & p_subGuid,service_ptr_t<service_base> p_callback) {
	mainmenu_commands::ptr ptr; t_uint32 index;
	if (!menu_item_resolver::g_resolve_main_command(p_guid, ptr, index)) return false;
	mainmenu_commands_v2::ptr v2;
	if (!ptr->service_query_t(v2)) return false;
	if (!v2->is_command_dynamic(index)) return false;
    bool rv = false;
    fb2k::crashOnException([&] {
        rv = v2->dynamic_execute(index, p_subGuid, p_callback);;
    }, "mainmenu_commands::dynamic_execute");
    return rv;
}
bool mainmenu_commands::g_execute(const GUID & p_guid,service_ptr_t<service_base> p_callback) {
	mainmenu_commands::ptr ptr; t_uint32 index;
	if (!menu_item_resolver::g_resolve_main_command(p_guid, ptr, index)) return false;
    fb2k::crashOnException( [&] {
        ptr->execute(index, p_callback);
    }, "mainmenu_commands::execute");
	return true;
}

bool mainmenu_commands::g_find_by_name(const char * p_name,GUID & p_guid) {
	pfc::string8_fastalloc temp;
	for( auto ptr : enumerate() ) {
		const t_uint32 count = ptr->get_command_count();
		for(t_uint32 n=0;n<count;n++) {
			ptr->get_name(n,temp);
			if (stricmp_utf8(temp,p_name) == 0) {
				p_guid = ptr->get_command(n);
				return true;
			}
		}
	}
	return false;

}


static bool dynamic_execute_recur(mainmenu_node::ptr node, const GUID & subID, service_ptr_t<service_base> callback) {
	switch(node->get_type()) {
		case mainmenu_node::type_command:
			if (subID == node->get_guid()) {
				node->execute(callback); return true;
			}
			break;
		case mainmenu_node::type_group:
			{
				const t_size total = node->get_children_count();
				for(t_size walk = 0; walk < total; ++walk) {
					if (dynamic_execute_recur(node->get_child(walk), subID, callback)) return true;
				}
			}
			break;
	}
	return false;
}

bool mainmenu_commands_v2::is_command_dynamic(t_uint32 index) {
	(void)index; return false;
}
mainmenu_node::ptr mainmenu_commands_v2::dynamic_instantiate(t_uint32 index) {
	(void)index; uBugCheck();
}

bool mainmenu_commands_v2::dynamic_execute(t_uint32 index, const GUID & subID, service_ptr_t<service_base> callback) {
	return dynamic_execute_recur(dynamic_instantiate(index), subID, callback);
}
