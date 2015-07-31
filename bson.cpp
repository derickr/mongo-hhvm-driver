/**
 *  Copyright 2015 MongoDB, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/execution-context.h"
#include "hphp/runtime/base/type-string.h"

#include "bson.h"
#include "utils.h"
#include <iostream>

#include "src/MongoDB/BSON/Binary.h"
#include "src/MongoDB/BSON/Javascript.h"
#include "src/MongoDB/BSON/ObjectID.h"
#include "src/MongoDB/BSON/Regex.h"
#include "src/MongoDB/BSON/Timestamp.h"
#include "src/MongoDB/BSON/UTCDateTime.h"

extern "C" {
#include "libbson/src/bson/bson.h"
#include "libmongoc/src/mongoc/mongoc.h"
}

namespace HPHP {
/* {{{ HHVM → BSON */

/* {{{ static strings used for conversions */
const StaticString
	s_root("root"),
	s_document("document"),
	s_object("object"),
	s_stdClass("stdClass"),
	s_array("array");
/* }}} */

int VariantToBsonConverter::_isPackedArray(const Array &a)
{
	int idx = 0, key_value = 0;

	for (ArrayIter iter(a); iter; ++iter) {
		Variant key(iter.first());

		if (!key.isInteger()) {
			return false;
		}

		key_value = key.toInt32(); 

		if (idx != key_value) {
			return false;
		}

		idx++;
	}
	return true;
}

VariantToBsonConverter::VariantToBsonConverter(const Variant& document, int flags)
{
	m_document = document;
	m_level = 0;
	m_flags = flags;
	m_out = NULL;
}

void VariantToBsonConverter::convert(bson_t *bson)
{
	if (m_document.isObject() || m_document.isArray()) {
		convertDocument(bson, NULL, m_document);
	} else {
		std::cout << "convert *unimplemented*: " << getDataTypeString(m_document.getType()).c_str() << "\n";
	}
}

void VariantToBsonConverter::convertElement(bson_t *bson, const char *key, Variant v)
{
	switch (v.getType()) {
		case KindOfUninit:
		case KindOfNull:
			convertNull(bson, key);
			break;
		case KindOfBoolean:
			convertBoolean(bson, key, v.toBoolean());
			break;
		case KindOfInt64:
			convertInt64(bson, key, v.toInt64());
			break;
		case KindOfDouble:
			convertDouble(bson, key, v.toDouble());
			break;
		case KindOfStaticString:
		case KindOfString:
			convertString(bson, key, v.toString());
			break;
		case KindOfArray:
		case KindOfObject:
			convertDocument(bson, key, v);
			break;
		case KindOfResource:
			throw MongoDriver::Utils::throwUnexpectedValueException("Got unsupported type 'resource'");
			return;
		default:
			break;
	}
}

void VariantToBsonConverter::convertNull(bson_t *bson, const char *key)
{
	bson_append_null(bson, key, -1);
};

void VariantToBsonConverter::convertBoolean(bson_t *bson, const char *key, bool v)
{
	bson_append_bool(bson, key, -1, v);
};

void VariantToBsonConverter::convertInt64(bson_t *bson, const char *key, int64_t v)
{
	if (v > INT_MAX || v < INT_MIN) {
		bson_append_int64(bson, key, -1, v);
	} else {
		bson_append_int32(bson, key, -1, (int32_t) v);
	}
};

void VariantToBsonConverter::convertDouble(bson_t *bson, const char *key, double v)
{
	bson_append_double(bson, key, -1, v);
};

void VariantToBsonConverter::convertString(bson_t *bson, const char *key, String v)
{
	bson_append_utf8(bson, key, -1, v.c_str(), v.size());
}

char *VariantToBsonConverter::_getUnmangledPropertyName(String key)
{
	const char *ckey = key.c_str();

	if (ckey[0] == '\0' && key.length()) {
		const char *cls = ckey + 1;
		if (*cls == '*') { // protected
			return strdup(ckey + 3);
		} else {
			int l = strlen(cls);
			return strdup(cls + l + 1);
		}
	} else {
		return strdup(ckey);
	}
}

void VariantToBsonConverter::convertDocument(bson_t *bson, const char *property_name, Variant v)
{
	bson_t child;
	int unmangle = 0;
	Array document;

	/* if we are not at a top-level, we need to check (and convert) special
	 * BSON types too */
	if (v.isObject()) {
		if (convertSpecialObject(bson, property_name, v.toObject())) {
			return;
		}
		/* The "convertSpecialObject" method didn't understand this type, so we
		 * will continue treating this as a normal document */
	}

	document = v.toObject()->o_toIterArray(null_string, ObjectData::PreserveRefs);

	if (_isPackedArray(document) && !v.isObject()) {
		if (property_name != NULL) {
			bson_append_array_begin(bson, property_name, -1, &child);
		}
	} else {
		unmangle = 1;
		if (property_name != NULL) {
			bson_append_document_begin(bson, property_name, -1, &child);
		}
	}

	for (ArrayIter iter(document); iter; ++iter) {
		Variant key(iter.first());
		const Variant& data(iter.secondRef());
		String s_key = key.toString();

		if (m_level == 0 && (m_flags & HIPPO_BSON_ADD_ID)) {

			if (strncmp(s_key.c_str(), "_id", s_key.length()) == 0) {
				m_flags &= !HIPPO_BSON_ADD_ID;
			}
		}

		m_level++;
		if (unmangle) {
			const char *unmangledName;

			unmangledName = _getUnmangledPropertyName(s_key);
			convertElement(property_name != NULL ? &child : bson, unmangledName, data);
			free((void*) unmangledName);
		} else {
			convertElement(property_name != NULL ? &child : bson, s_key.c_str(), data);
		}
		m_level--;
	}

	if (m_level == 0 && (m_flags & HIPPO_BSON_ADD_ID)) {
		bson_oid_t oid;

		bson_oid_init(&oid, NULL);
		bson_append_oid(bson, "_id", strlen("_id"), &oid);

		if (m_flags & HIPPO_BSON_RETURN_ID) {
			m_out = bson_new();
			bson_append_oid(m_out, "_id", sizeof("_id")-1, &oid);
		}
	}

	if (property_name != NULL) {
		if (_isPackedArray(document)) {
			bson_append_array_end(bson, &child);
		} else {
			bson_append_document_end(bson, &child);
		}
	}
}

/* {{{ Serialization of types */
const StaticString s_MongoDriverBsonType_className("MongoDB\\BSON\\Type");
const StaticString s_MongoDriverBsonPersistable_className("MongoDB\\BSON\\Persistable");
const StaticString s_MongoDriverBsonSerializable_className("MongoDB\\BSON\\Serializable");
const StaticString s_MongoDriverBsonUnserializable_className("MongoDB\\BSON\\Unserializable");
const StaticString s_MongoDriverBsonSerializable_functionName("bsonSerialize");
const StaticString s_MongoDriverBsonUnserializable_functionName("bsonUnserialize");
const StaticString s_MongoDriverBsonODM_fieldName("__pclass");

/* {{{ MongoDriver\BSON\Binary */
void VariantToBsonConverter::_convertBinary(bson_t *bson, const char *key, Object v)
{
	String data = v.o_get(s_MongoBsonBinary_data, false, s_MongoBsonBinary_className);
	int64_t subType = v.o_get(s_MongoBsonBinary_subType, false, s_MongoBsonBinary_className).toInt64();

	bson_append_binary(bson, key, -1, (bson_subtype_t) subType, (const unsigned char*) data.c_str(), data.length());
}
/* }}} */

/* {{{ MongoDriver\BSON\Javascript */
void VariantToBsonConverter::_convertJavascript(bson_t *bson, const char *key, Object v)
{
	bson_t *scope_bson;
	String code = v.o_get(s_MongoBsonJavascript_code, false, s_MongoBsonJavascript_className);
	auto scope = v.o_get(s_MongoBsonJavascript_scope, false, s_MongoBsonJavascript_className);

	if (scope.isObject() || scope.isArray()) {
		/* Convert scope to document */
		VariantToBsonConverter converter(scope, HIPPO_BSON_NO_FLAGS);
		scope_bson = bson_new();
		converter.convert(scope_bson);

		bson_append_code_with_scope(bson, key, -1, (const char*) code.c_str(), scope_bson);
	} else {
		bson_append_code(bson, key, -1, (const char*) code.c_str());
	}
}
/* }}} */

/* {{{ MongoDriver\BSON\MaxKey */
const StaticString s_MongoBsonMaxKey_className("MongoDB\\BSON\\MaxKey");

void VariantToBsonConverter::_convertMaxKey(bson_t *bson, const char *key, Object v)
{
	bson_append_maxkey(bson, key, -1);
}
/* }}} */

/* {{{ MongoDriver\BSON\MinKey */
const StaticString s_MongoBsonMinKey_className("MongoDB\\BSON\\MinKey");

void VariantToBsonConverter::_convertMinKey(bson_t *bson, const char *key, Object v)
{
	bson_append_minkey(bson, key, -1);
}
/* }}} */

/* {{{ MongoDriver\BSON\ObjectID */
void VariantToBsonConverter::_convertObjectID(bson_t *bson, const char *key, Object v)
{
    MongoDBBsonObjectIDData* data = Native::data<MongoDBBsonObjectIDData>(v.get());

	bson_append_oid(bson, key, -1, &data->m_oid);
}
/* }}} */

/* {{{ MongoDriver\BSON\Regex */

void VariantToBsonConverter::_convertRegex(bson_t *bson, const char *key, Object v)
{
	String regex = v.o_get(s_MongoBsonRegex_pattern, false, s_MongoBsonRegex_className);
	String flags = v.o_get(s_MongoBsonRegex_flags, false, s_MongoBsonRegex_className);

	bson_append_regex(bson, key, -1, regex.c_str(), flags.c_str());
}
/* }}} */

/* {{{ MongoDriver\BSON\Timestamp */
void VariantToBsonConverter::_convertTimestamp(bson_t *bson, const char *key, Object v)
{
	int32_t timestamp = v.o_get(s_MongoBsonTimestamp_timestamp, false, s_MongoBsonTimestamp_className).toInt32();
	int32_t increment = v.o_get(s_MongoBsonTimestamp_increment, false, s_MongoBsonTimestamp_className).toInt32();

	bson_append_timestamp(bson, key, -1, timestamp, increment);
}

/* {{{ MongoDriver\BSON\UTCDateTime */
void VariantToBsonConverter::_convertUTCDateTime(bson_t *bson, const char *key, Object v)
{
	int64_t milliseconds = v.o_get(s_MongoBsonUTCDateTime_milliseconds, false, s_MongoBsonUTCDateTime_className).toInt64();

	bson_append_date_time(bson, key, -1, milliseconds);
}
/* }}} */

/* {{{ Special objects that implement MongoDB\BSON\Serializable */
void VariantToBsonConverter::_convertSerializable(bson_t *bson, const char *key, Object v)
{
	Variant result;
	Array   properties;
	TypedValue args[1] = { *(Variant(v)).asCell() };
	Class *cls;
	Func *m;

	cls = v.get()->getVMClass();
	m = cls->lookupMethod(s_MongoDriverBsonSerializable_functionName.get());
	
	g_context->invokeFuncFew(
		result.asTypedValue(),
		m,
		v.get(),
		nullptr,
		1, args
	);

	if ( ! (
			result.isArray() || 
			(result.isObject() && result.toObject().instanceof(s_stdClass))
		)
	) {
		StringBuffer buf;
		buf.printf(
			"Expected %s::%s() to return an array or stdClass, %s given",
			cls->nameStr().c_str(),
			s_MongoDriverBsonSerializable_functionName.data(),
			result.isObject() ? result.toObject()->getVMClass()->nameStr().c_str() : HPHP::getDataTypeString(result.getType()).data()
		);
		Variant full_name = buf.detach();

		throw MongoDriver::Utils::throwUnexpectedValueException((char*) full_name.toString().c_str());
	}

	/* Convert to array so that we can handle it well */
	properties = result.toArray();

	if (m_level > 0 || (m_flags & HIPPO_BSON_NO_ROOT_ODS) == 0) {
		if (v.instanceof(s_MongoDriverBsonPersistable_className)) {
			const char *class_name = cls->nameStr().c_str();
			ObjectData *obj = createMongoBsonBinaryObject(
				(const uint8_t *) class_name,
				strlen(class_name),
				(bson_subtype_t) 0x80
			);
			properties.add(String(s_MongoDriverBsonODM_fieldName), obj);
		}
	}

	convertDocument(bson, key, result.isObject() ? Variant(Variant(properties).toObject()) : Variant(properties));
}
/* }}} */

/* }}} */

bool VariantToBsonConverter::convertSpecialObject(bson_t *bson, const char *key, Object v)
{
	if (v.instanceof(s_MongoDriverBsonType_className)) {
		if (v.instanceof(s_MongoBsonBinary_className)) {
			_convertBinary(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonJavascript_className)) {
			_convertJavascript(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonMaxKey_className)) {
			_convertMaxKey(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonMinKey_className)) {
			_convertMinKey(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonObjectID_className)) {
			_convertObjectID(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonRegex_className)) {
			_convertRegex(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonTimestamp_className)) {
			_convertTimestamp(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoBsonUTCDateTime_className)) {
			_convertUTCDateTime(bson, key, v);
			return true;
		}
		if (v.instanceof(s_MongoDriverBsonSerializable_className)) {
			_convertSerializable(bson, key, v);
			return true;
		}
	}
	return false;
}
/* }}} */
/* }}} */

/* {{{ BSON → HHVM */
BsonToVariantConverter::BsonToVariantConverter(const unsigned char *data, int data_len, hippo_bson_conversion_options_t options)
{
	m_reader = bson_reader_new_from_data(data, data_len);
	m_options = options;
}

/* {{{ Visitors */
void hippo_bson_visit_corrupt(const bson_iter_t *iter __attribute__((unused)), void *data)
{
	std::cout << "converting corrupt\n";
}

bool hippo_bson_visit_double(const bson_iter_t *iter __attribute__((unused)), const char *key, double v_double, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(v_double));
	return false;
}

bool hippo_bson_visit_utf8(const bson_iter_t *iter __attribute__((unused)), const char *key, size_t v_utf8_len, const char *v_utf8, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(v_utf8));
	return false;
}

bool hippo_bson_visit_document(const bson_iter_t *iter __attribute__((unused)), const char *key, const bson_t *v_document, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	Variant document_v;

	state->options.current_compound_type = HIPPO_BSONTYPE_DOCUMENT;

	BsonToVariantConverter converter(bson_get_data(v_document), v_document->len, state->options);
	converter.convert(&document_v);

	state->zchild.add(String(key), document_v);

	return false;
}

bool hippo_bson_visit_array(const bson_iter_t *iter __attribute__((unused)), const char *key, const bson_t *v_array, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	Variant array_v;

	state->options.current_compound_type = HIPPO_BSONTYPE_ARRAY;

	BsonToVariantConverter converter(bson_get_data(v_array), v_array->len, state->options);
	converter.convert(&array_v);

	state->zchild.add(String(key), array_v);

	return false;
}

bool hippo_bson_visit_binary(const bson_iter_t *iter __attribute__((unused)), const char *key, bson_subtype_t v_subtype, size_t v_binary_len, const uint8_t *v_binary, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	ObjectData *obj;

	obj = createMongoBsonBinaryObject(v_binary, v_binary_len, v_subtype);

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_oid(const bson_iter_t *iter __attribute__((unused)), const char *key, const bson_oid_t *v_oid, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_objectId;

	c_objectId = Unit::lookupClass(s_MongoBsonObjectID_className.get());
	assert(c_objectId);
	ObjectData* obj = ObjectData::newInstance(c_objectId);

	MongoDBBsonObjectIDData* obj_data = Native::data<MongoDBBsonObjectIDData>(obj);
	bson_oid_copy(v_oid, &obj_data->m_oid);

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_bool(const bson_iter_t *iter __attribute__((unused)), const char *key, bool v_bool, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(v_bool));
	return false;
}

bool hippo_bson_visit_date_time(const bson_iter_t *iter __attribute__((unused)), const char *key, int64_t msec_since_epoch, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_datetime;

	c_datetime = Unit::lookupClass(s_MongoBsonUTCDateTime_className.get());
	assert(c_datetime);
	ObjectData* obj = ObjectData::newInstance(c_datetime);

	obj->o_set(s_MongoBsonUTCDateTime_milliseconds, Variant(msec_since_epoch), s_MongoBsonUTCDateTime_className.get());

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_null(const bson_iter_t *iter __attribute__((unused)), const char *key, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(Variant::NullInit()));
	return false;
}

bool hippo_bson_visit_regex(const bson_iter_t *iter __attribute__((unused)), const char *key, const char *v_regex, const char *v_options, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_regex;

	c_regex = Unit::lookupClass(s_MongoBsonRegex_className.get());
	assert(c_regex);
	ObjectData* obj = ObjectData::newInstance(c_regex);

	obj->o_set(s_MongoBsonRegex_pattern, Variant(v_regex), s_MongoBsonRegex_className.get());
	obj->o_set(s_MongoBsonRegex_flags, Variant(v_options), s_MongoBsonRegex_className.get());

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_code(const bson_iter_t *iter __attribute__((unused)), const char *key, size_t v_code_len, const char *v_code, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_code;
	String s;
	unsigned char *data_s;

	s = String(v_code_len, ReserveString);
	data_s = (unsigned char*) s.bufferSlice().ptr;
	memcpy(data_s, v_code, v_code_len);
	s.setSize(v_code_len);

	c_code = Unit::lookupClass(s_MongoBsonJavascript_className.get());
	assert(c_code);
	ObjectData* obj = ObjectData::newInstance(c_code);

	obj->o_set(s_MongoBsonJavascript_code, s, s_MongoBsonJavascript_className.get());

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_codewscope(const bson_iter_t *iter __attribute__((unused)), const char *key, size_t v_code_len, const char *v_code, const bson_t *v_scope, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_code;
	String s;
	unsigned char *data_s;
	Variant scope_v;

	/* code */
	s = String(v_code_len, ReserveString);
	data_s = (unsigned char*) s.bufferSlice().ptr;
	memcpy(data_s, v_code, v_code_len);
	s.setSize(v_code_len);

	/* scope */
	BsonToVariantConverter converter(bson_get_data(v_scope), v_scope->len, state->options);
	converter.convert(&scope_v);

	/* create object */
	c_code = Unit::lookupClass(s_MongoBsonJavascript_className.get());
	assert(c_code);
	ObjectData* obj = ObjectData::newInstance(c_code);

	/* set properties */
	obj->o_set(s_MongoBsonJavascript_code, s, s_MongoBsonJavascript_className.get());
	obj->o_set(s_MongoBsonJavascript_scope, scope_v, s_MongoBsonJavascript_className.get());

	/* add to array */
	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_int32(const bson_iter_t *iter __attribute__((unused)), const char *key, int32_t v_int32, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(v_int32));
	return false;
}

bool hippo_bson_visit_timestamp(const bson_iter_t *iter __attribute__((unused)), const char *key, uint32_t v_timestamp, uint32_t v_increment, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_timestamp;

	c_timestamp = Unit::lookupClass(s_MongoBsonTimestamp_className.get());
	assert(c_timestamp);
	ObjectData* obj = ObjectData::newInstance(c_timestamp);

	obj->o_set(s_MongoBsonTimestamp_timestamp, Variant((uint64_t) v_timestamp), s_MongoBsonTimestamp_className.get());
	obj->o_set(s_MongoBsonTimestamp_increment, Variant((uint64_t) v_increment), s_MongoBsonTimestamp_className.get());

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_int64(const bson_iter_t *iter __attribute__((unused)), const char *key, int64_t v_int64, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;

	state->zchild.add(String(key), Variant(v_int64));
	return false;
}

bool hippo_bson_visit_maxkey(const bson_iter_t *iter __attribute__((unused)), const char *key, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_objectId;

	c_objectId = Unit::lookupClass(s_MongoBsonMaxKey_className.get());
	assert(c_objectId);
	ObjectData* obj = ObjectData::newInstance(c_objectId);

	state->zchild.add(String(key), Variant(obj));

	return false;
}

bool hippo_bson_visit_minkey(const bson_iter_t *iter __attribute__((unused)), const char *key, void *data)
{
	hippo_bson_state *state = (hippo_bson_state*) data;
	static Class* c_objectId;

	c_objectId = Unit::lookupClass(s_MongoBsonMinKey_className.get());
	assert(c_objectId);
	ObjectData* obj = ObjectData::newInstance(c_objectId);

	state->zchild.add(String(key), Variant(obj));

	return false;
}


static const bson_visitor_t hippo_bson_visitors = {
	NULL /* hippo_phongo_bson_visit_before*/,
	NULL /*hippo_phongo_bson_visit_after*/,
	hippo_bson_visit_corrupt,
	hippo_bson_visit_double,
	hippo_bson_visit_utf8,
	hippo_bson_visit_document,
	hippo_bson_visit_array,
	hippo_bson_visit_binary,
	NULL /*hippo_bson_visit_undefined*/,
	hippo_bson_visit_oid,
	hippo_bson_visit_bool,
	hippo_bson_visit_date_time,
	hippo_bson_visit_null,
	hippo_bson_visit_regex,
	NULL /*hippo_bson_visit_dbpointer*/,
	hippo_bson_visit_code,
	NULL /*hippo_bson_visit_symbol*/,
	hippo_bson_visit_codewscope,
	hippo_bson_visit_int32,
	hippo_bson_visit_timestamp,
	hippo_bson_visit_int64,
	hippo_bson_visit_maxkey,
	hippo_bson_visit_minkey,
	{ NULL }
};
/* }}} */


bool BsonToVariantConverter::convert(Variant *v)
{
	bson_iter_t   iter;
	bool          eof = false;
	bool          havePclass;
	const bson_t *b;
	int type_descriminator;
	String named_class;

	m_state.zchild = NULL;

	/* Determine which descriminator to use */
	switch (m_options.current_compound_type) {
		case HIPPO_BSONTYPE_ARRAY:
			type_descriminator = m_options.array_type;
			named_class = m_options.array_class_name;
			break;

		case HIPPO_BSONTYPE_ROOT:
			type_descriminator = m_options.root_type;
			named_class = m_options.root_class_name;
			break;

		case HIPPO_BSONTYPE_DOCUMENT:
			type_descriminator = m_options.document_type;
			named_class = m_options.document_class_name;
			break;
	}

	if (!(b = bson_reader_read(m_reader, &eof))) {
		throw MongoDriver::Utils::throwUnexpectedValueException("Could not read document from BSON reader");
		return false;
	}

	if (!bson_iter_init(&iter, b)) {
		bson_reader_destroy(m_reader);
		return false;
	}

	m_state.zchild = Array::Create();
	m_state.options = m_options;

	bson_iter_visit_all(&iter, &hippo_bson_visitors, &m_state);

	/* Set "root" to false */
	havePclass = false;

	if (
		(
			type_descriminator == HIPPO_TYPEMAP_DEFAULT ||
			type_descriminator == HIPPO_TYPEMAP_NAMEDCLASS
		) &&
		m_state.zchild.exists(s_MongoDriverBsonODM_fieldName) &&
		m_state.zchild[s_MongoDriverBsonODM_fieldName].isObject() &&
		m_state.zchild[s_MongoDriverBsonODM_fieldName].toObject().instanceof(s_MongoBsonBinary_className) &&
		m_state.zchild[s_MongoDriverBsonODM_fieldName].toObject().o_get(s_MongoBsonBinary_subType, false, s_MongoBsonBinary_className).toInt64() == 0x80
	) {
		havePclass = true;
	}

	if (type_descriminator == HIPPO_TYPEMAP_NAMEDCLASS) {
		static Class* c_class;
		static Class* c_unserializable_interface;
		static Class* c_persistable_interface;
		Object obj;
		Variant result;
		bool useTypeMap = true;
		c_unserializable_interface = Unit::lookupClass(s_MongoDriverBsonUnserializable_className.get());
		c_persistable_interface = Unit::lookupClass(s_MongoDriverBsonPersistable_className.get());

		/* If we have a __pclass, and the class exists, and the class
		 * implements MongoDB\BSON\Persitable, we use that class name. */
		if (havePclass) {
			String class_name = m_state.zchild[s_MongoDriverBsonODM_fieldName].toObject().o_get(
				s_MongoBsonBinary_data, false, s_MongoBsonBinary_className
			);

			/* Lookup class and instantiate object, but if we can't find the class,
			 * make it a stdClass */
			c_class = Unit::lookupClass(class_name.get());
			if (c_class && isNormalClass(c_class) && c_class->classof(c_persistable_interface)) {
				/* Instantiate */
				obj = Object{c_class};
				useTypeMap = false;
				*v = Variant(obj);
			}
		}

		if (useTypeMap) {
			c_class = Unit::lookupClass(named_class.get());
			if (c_class && isNormalClass(c_class) && c_class->classof(c_unserializable_interface)) {
				/* Instantiate */
				obj = Object{c_class};
				useTypeMap = false;
				*v = Variant(obj);
			}
		}

		/* If the type map didn't match with a suitable class, we bail out */
		if (useTypeMap) {
			throw MongoDriver::Utils::throwInvalidArgumentException("The typemap does not provide a class that implements MongoDB\\BSON\\Unserializable");
		}

		{
			/* Setup arguments for bsonUnserialize call */
			TypedValue args[1] = { *(Variant(m_state.zchild)).asCell() };

			/* Call bsonUnserialize on the object */
			Func *m = c_class->lookupMethod(s_MongoDriverBsonUnserializable_functionName.get());

			g_context->invokeFuncFew(
				result.asTypedValue(),
				m,
				obj.get(),
				nullptr,
				1, args
			);

			*v = Variant(obj);
		}
	} else if (havePclass) {
		static Class* c_class;
		Variant result;

		String class_name = m_state.zchild[s_MongoDriverBsonODM_fieldName].toObject().o_get(
			s_MongoBsonBinary_data, false, s_MongoBsonBinary_className
		);
		TypedValue args[1] = { *(Variant(m_state.zchild)).asCell() };

		/* Lookup class and instantiate object, but if we can't find the class,
		 * make it a stdClass */
		c_class = Unit::lookupClass(class_name.get());
		if (!c_class) {
			*v = Variant(Variant(m_state.zchild).toObject());
			return true;
		}

		/* Instantiate */
		ObjectData *obj = ObjectData::newInstance(c_class);

		/* If the class does not implement Persistable, make it a stdClass */
		if (!obj->instanceof(s_MongoDriverBsonPersistable_className)) {
			*v = Variant(Variant(m_state.zchild).toObject());
			return true;
		}

		/* Call bsonUnserialize on the object */
		Func *m = c_class->lookupMethod(s_MongoDriverBsonUnserializable_functionName.get());

		g_context->invokeFuncFew(
			result.asTypedValue(),
			m,
			obj,
			nullptr,
			1, args
		);

		*v = Variant(obj);
	} else if (type_descriminator == HIPPO_TYPEMAP_DEFAULT) {
		if (m_options.current_compound_type == HIPPO_BSONTYPE_ARRAY) {
			*v = Variant(Variant(m_state.zchild).toArray());
		} else if (m_options.current_compound_type == HIPPO_BSONTYPE_ROOT) {
			*v = Variant(Variant(m_state.zchild).toObject());
		} else if (m_options.current_compound_type == HIPPO_BSONTYPE_DOCUMENT) {
			*v = Variant(Variant(m_state.zchild).toObject());
		}
	} else if (type_descriminator == HIPPO_TYPEMAP_STDCLASS) {
		*v = Variant(Variant(m_state.zchild).toObject());
	} else if (type_descriminator == HIPPO_TYPEMAP_ARRAY) {
		*v = Variant(m_state.zchild);
	} else {
		assert(NULL);
	}

	if (bson_reader_read(m_reader, &eof) || !eof) {
		bson_reader_destroy(m_reader);
		throw MongoDriver::Utils::throwUnexpectedValueException("Reading document did not exhaust input buffer");
		return false;
	}

	bson_reader_destroy(m_reader);
	return true;
}
/* }}} */

/* {{{ TypeMap helper functions */
#define CASECMP(a,b) (bstrcasecmp((a).data(), (a).size(), (b).data(), (b).size()) == 0)

void parseTypeMap(hippo_bson_conversion_options_t *options, const Array &typemap)
{
	if (typemap.exists(s_root)) {
		String root_type;

		root_type = typemap[s_root].toString();

		if (CASECMP(root_type, s_object) || CASECMP(root_type, s_stdClass)) {
			options->root_type = HIPPO_TYPEMAP_STDCLASS;
		} else if (CASECMP(root_type, s_array)) {
			options->root_type = HIPPO_TYPEMAP_ARRAY;
		} else {
			options->root_type = HIPPO_TYPEMAP_NAMEDCLASS;
			options->root_class_name = root_type;
		}
	}

	if (typemap.exists(s_document)) {
		String document_type;

		document_type = typemap[s_document].toString();

		if (CASECMP(document_type, s_object) || CASECMP(document_type, s_stdClass)) {
			options->document_type = HIPPO_TYPEMAP_STDCLASS;
		} else if (CASECMP(document_type, s_array)) {
			options->document_type = HIPPO_TYPEMAP_ARRAY;
		} else {
			options->document_type = HIPPO_TYPEMAP_NAMEDCLASS;
			options->document_class_name = document_type;
		}
	}

	if (typemap.exists(s_array)) {
		String array_type;

		array_type = typemap[s_array].toString();

		if (CASECMP(array_type, s_object) || CASECMP(array_type, s_stdClass)) {
			options->array_type = HIPPO_TYPEMAP_STDCLASS;
		} else if (CASECMP(array_type, s_array)) {
			options->array_type = HIPPO_TYPEMAP_ARRAY;
		} else {
			options->array_type = HIPPO_TYPEMAP_NAMEDCLASS;
			options->array_class_name = array_type;
		}
	}
}

/* }}} */

} /* namespace HPHP */
