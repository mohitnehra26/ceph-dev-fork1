#include <string.h>

#include <iostream>
#include <map>

#include "include/types.h"

#include "rgw_acl_s3.h"
#include "rgw_user.h"

#define DOUT_SUBSYS rgw

using namespace std;


#define RGW_URI_ALL_USERS	"http://acs.amazonaws.com/groups/global/AllUsers"
#define RGW_URI_AUTH_USERS	"http://acs.amazonaws.com/groups/global/AuthenticatedUsers"

static string rgw_uri_all_users = RGW_URI_ALL_USERS;
static string rgw_uri_auth_users = RGW_URI_AUTH_USERS;

void ACLPermission_S3::to_xml(ostream& out)
{
  if ((flags & RGW_PERM_FULL_CONTROL) == RGW_PERM_FULL_CONTROL) {
   out << "<Permission>FULL_CONTROL</Permission>";
  } else {
    if (flags & RGW_PERM_READ)
      out << "<Permission>READ</Permission>";
    if (flags & RGW_PERM_WRITE)
      out << "<Permission>WRITE</Permission>";
    if (flags & RGW_PERM_READ_ACP)
      out << "<Permission>READ_ACP</Permission>";
    if (flags & RGW_PERM_WRITE_ACP)
      out << "<Permission>WRITE_ACP</Permission>";
  }
}

bool ACLPermission_S3::
xml_end(const char *el)
{
  const char *s = data.c_str();
  if (strcasecmp(s, "READ") == 0) {
    flags |= RGW_PERM_READ;
    return true;
  } else if (strcasecmp(s, "WRITE") == 0) {
    flags |= RGW_PERM_WRITE;
    return true;
  } else if (strcasecmp(s, "READ_ACP") == 0) {
    flags |= RGW_PERM_READ_ACP;
    return true;
  } else if (strcasecmp(s, "WRITE_ACP") == 0) {
    flags |= RGW_PERM_WRITE_ACP;
    return true;
  } else if (strcasecmp(s, "FULL_CONTROL") == 0) {
    flags |= RGW_PERM_FULL_CONTROL;
    return true;
  }
  return false;
}


class ACLGranteeType_S3 {
public:
  static const char *to_string(ACLGranteeType& type) {
    switch (type.get_type()) {
    case ACL_TYPE_CANON_USER:
      return "CanonicalUser";
    case ACL_TYPE_EMAIL_USER:
      return "AmazonCustomerByEmail";
    case ACL_TYPE_GROUP:
      return "Group";
     default:
      return "unknown";
    }
  }

  static void set(const char *s, ACLGranteeType& type) {
    if (!s) {
      type.set(ACL_TYPE_UNKNOWN);
      return;
    }
    if (strcmp(s, "CanonicalUser") == 0)
      type.set(ACL_TYPE_CANON_USER);
    else if (strcmp(s, "AmazonCustomerByEmail") == 0)
      type.set(ACL_TYPE_EMAIL_USER);
    else if (strcmp(s, "Group") == 0)
      type.set(ACL_TYPE_GROUP);
    else
      type.set(ACL_TYPE_UNKNOWN);
  }
};

class ACLID_S3 : public XMLObj
{
public:
  ACLID_S3() {}
  ~ACLID_S3() {}
  string& to_str() { return data; }
};

class ACLURI_S3 : public XMLObj
{
public:
  ACLURI_S3() {}
  ~ACLURI_S3() {}
};

class ACLEmail_S3 : public XMLObj
{
public:
  ACLEmail_S3() {}
  ~ACLEmail_S3() {}
};

class ACLDisplayName_S3 : public XMLObj
{
public:
 ACLDisplayName_S3() {}
 ~ACLDisplayName_S3() {}
};

bool ACLOwner_S3::xml_end(const char *el) {
  ACLID_S3 *acl_id = (ACLID_S3 *)find_first("ID");
  ACLID_S3 *acl_name = (ACLID_S3 *)find_first("DisplayName");

  // ID is mandatory
  if (!acl_id)
    return false;
  id = acl_id->get_data();

  // DisplayName is optional
  if (acl_name)
    display_name = acl_name->get_data();
  else
    display_name = "";

  return true;
}

bool ACLGrant_S3::xml_end(const char *el) {
  ACLGrantee_S3 *acl_grantee;
  ACLID_S3 *acl_id;
  ACLURI_S3 *acl_uri;
  ACLEmail_S3 *acl_email;
  ACLPermission_S3 *acl_permission;
  ACLDisplayName_S3 *acl_name;

  acl_grantee = (ACLGrantee_S3 *)find_first("Grantee");
  if (!acl_grantee)
    return false;
  string type_str;
  if (!acl_grantee->get_attr("xsi:type", type_str))
    return false;
  ACLGranteeType_S3::set(type_str.c_str(), type);
  
  acl_permission = (ACLPermission_S3 *)find_first("Permission");
  if (!acl_permission)
    return false;

  permission = *acl_permission;

  id.clear();
  name.clear();
  uri.clear();
  email.clear();

  switch (type.get_type()) {
  case ACL_TYPE_CANON_USER:
    acl_id = (ACLID_S3 *)acl_grantee->find_first("ID");
    if (!acl_id)
      return false;
    id = acl_id->to_str();
    acl_name = (ACLDisplayName_S3 *)acl_grantee->find_first("DisplayName");
    if (acl_name)
      name = acl_name->get_data();
    break;
  case ACL_TYPE_GROUP:
    acl_uri = (ACLURI_S3 *)acl_grantee->find_first("URI");
    if (!acl_uri)
      return false;
    uri = acl_uri->get_data();
    break;
  case ACL_TYPE_EMAIL_USER:
    acl_email = (ACLEmail_S3 *)acl_grantee->find_first("EmailAddress");
    if (!acl_email)
      return false;
    email = acl_email->get_data();
    break;
  default:
    // unknown user type
    return false;
  };
  return true;
}

void ACLGrant_S3::to_xml(ostream& out) {
  out << "<Grant>" <<
         "<Grantee xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:type=\"" << ACLGranteeType_S3::to_string(type) << "\">";
  switch (type.get_type()) {
  case ACL_TYPE_CANON_USER:
    out << "<ID>" << id << "</ID>" <<
           "<DisplayName>" << name << "</DisplayName>";
    break;
  case ACL_TYPE_EMAIL_USER:
    out << "<EmailAddress>" << email << "</EmailAddress>";
    break;
  case ACL_TYPE_GROUP:
     out << "<URI>" << uri << "</URI>";
    break;
  default:
    break;
  }
  out << "</Grantee>";
  ACLPermission_S3& perm = static_cast<ACLPermission_S3 &>(permission);
  perm.to_xml(out);
  out << "</Grant>";
}

bool RGWAccessControlList_S3::xml_end(const char *el) {
  XMLObjIter iter = find("Grant");
  ACLGrant *grant = (ACLGrant *)iter.get_next();
  while (grant) {
    add_grant(grant);
    grant = (ACLGrant *)iter.get_next();
  }
  return true;
}

bool RGWAccessControlList_S3::create_canned(string id, string name, string canned_acl)
{
  acl_user_map.clear();
  grant_map.clear();

  /* owner gets full control */
  ACLGrant grant;
  grant.set_canon(id, name, RGW_PERM_FULL_CONTROL);
  add_grant(&grant);

  if (canned_acl.size() == 0 || canned_acl.compare("private") == 0) {
    return true;
  }

  ACLGrant group_grant;
  if (canned_acl.compare("public-read") == 0) {
    group_grant.set_group(rgw_uri_all_users, RGW_PERM_READ);
    add_grant(&group_grant);
  } else if (canned_acl.compare("public-read-write") == 0) {
    group_grant.set_group(rgw_uri_all_users, RGW_PERM_READ);
    add_grant(&group_grant);
    group_grant.set_group(rgw_uri_all_users, RGW_PERM_WRITE);
    add_grant(&group_grant);
  } else if (canned_acl.compare("authenticated-read") == 0) {
    group_grant.set_group(rgw_uri_auth_users, RGW_PERM_READ);
    add_grant(&group_grant);
  } else {
    return false;
  }

  return true;
}

bool RGWAccessControlPolicy_S3::xml_end(const char *el) {
  RGWAccessControlList_S3 *s3acl =
      (RGWAccessControlList_S3 *)find_first("AccessControlList");
  acl = *s3acl;

  ACLOwner *owner_p = (ACLOwner*)find_first("Owner");
  if (!owner_p)
    return false;
  owner = *owner_p;
  return true;
}

/*
  can only be called on object that was parsed
 */
int RGWAccessControlPolicy_S3::rebuild(ACLOwner *owner, RGWAccessControlPolicy& dest)
{
dout(0) << __FILE__ << ":" << __LINE__ << " owner=" << (void *)owner << dendl;
  if (!owner)
    return -EINVAL;

dout(0) << __FILE__ << ":" << __LINE__ << dendl;
  ACLOwner *requested_owner = (ACLOwner *)find_first("Owner");
dout(0) << __FILE__ << ":" << __LINE__ << " requested_owner=" << (void *)requested_owner << dendl;
  if (requested_owner && requested_owner->get_id().compare(owner->get_id()) != 0) {
    return -EPERM;
  }

  RGWUserInfo owner_info;
  if (rgw_get_user_info_by_uid(owner->get_id(), owner_info) < 0) {
    dout(10) << "owner info does not exist" << dendl;
    return -EINVAL;
  }
  ACLOwner& dest_owner = dest.get_owner();
  dest_owner.set_id(owner->get_id());
  dest_owner.set_name(owner_info.display_name);

  dout(20) << "owner id=" << owner->get_id() << dendl;
  dout(20) << "dest owner id=" << dest.get_owner().get_id() << dendl;

  RGWAccessControlList& dst_acl = dest.get_acl();

  multimap<string, ACLGrant>& grant_map = acl.get_grant_map();
  multimap<string, ACLGrant>::iterator iter;
  for (iter = grant_map.begin(); iter != grant_map.end(); ++iter) {
    ACLGrant& src_grant = iter->second;
    ACLGranteeType& type = src_grant.get_type();
    ACLGrant new_grant;
    bool grant_ok = false;
    string uid;
    RGWUserInfo grant_user;
    switch (type.get_type()) {
    case ACL_TYPE_EMAIL_USER:
      {
        string email = src_grant.get_id();
        dout(10) << "grant user email=" << email << dendl;
        if (rgw_get_user_info_by_email(email, grant_user) < 0) {
          dout(10) << "grant user email not found or other error" << dendl;
          return -ERR_UNRESOLVABLE_EMAIL;
        }
        uid = grant_user.user_id;
      }
    case ACL_TYPE_CANON_USER:
      {
        if (type.get_type() == ACL_TYPE_CANON_USER)
          uid = src_grant.get_id();
    
        if (grant_user.user_id.empty() && rgw_get_user_info_by_uid(uid, grant_user) < 0) {
          dout(10) << "grant user does not exist:" << uid << dendl;
          return -EINVAL;
        } else {
          ACLPermission& perm = src_grant.get_permission();
          new_grant.set_canon(uid, grant_user.display_name, perm.get_permissions());
          grant_ok = true;
          dout(10) << "new grant: " << new_grant.get_id() << ":" << grant_user.display_name << dendl;
        }
      }
      break;
    case ACL_TYPE_GROUP:
      {
        string group = src_grant.get_id();
        if (group.compare(RGW_URI_ALL_USERS) == 0 ||
            group.compare(RGW_URI_AUTH_USERS) == 0) {
          new_grant = src_grant;
          grant_ok = true;
          dout(10) << "new grant: " << new_grant.get_id() << dendl;
        } else {
          dout(10) << "grant group does not exist:" << group << dendl;
          return -EINVAL;
        }
      }
    default:
      break;
    }
    if (grant_ok) {
      dst_acl.add_grant(&new_grant);
    }
  }

  return 0; 
}

bool RGWAccessControlPolicy_S3::compare_group_name(string& id, ACLGroupTypeEnum group)
{
  switch (group) {
  case ACL_GROUP_ALL_USERS:
    return (id.compare(rgw_uri_all_users) == 0);
  case ACL_GROUP_AUTHENTICATED_USERS:
    return (id.compare(rgw_uri_auth_users) == 0);
  default:
    return id.empty();
  }

  // shouldn't get here
  return false;
}

XMLObj *RGWACLXMLParser_S3::alloc_obj(const char *el)
{
  XMLObj * obj = NULL;
  if (strcmp(el, "AccessControlPolicy") == 0) {
    obj = new RGWAccessControlPolicy_S3();
  } else if (strcmp(el, "Owner") == 0) {
    obj = new ACLOwner_S3();
  } else if (strcmp(el, "AccessControlList") == 0) {
    obj = new RGWAccessControlList_S3();
  } else if (strcmp(el, "ID") == 0) {
    obj = new ACLID_S3();
  } else if (strcmp(el, "DisplayName") == 0) {
    obj = new ACLDisplayName_S3();
  } else if (strcmp(el, "Grant") == 0) {
    obj = new ACLGrant_S3();
  } else if (strcmp(el, "Grantee") == 0) {
    obj = new ACLGrantee_S3();
  } else if (strcmp(el, "Permission") == 0) {
    obj = new ACLPermission_S3();
  } else if (strcmp(el, "URI") == 0) {
    obj = new ACLURI_S3();
  } else if (strcmp(el, "EmailAddress") == 0) {
    obj = new ACLEmail_S3();
  }

  return obj;
}

