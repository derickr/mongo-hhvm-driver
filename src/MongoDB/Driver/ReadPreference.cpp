/**
 *  Copyright 2014-2015 MongoDB, Inc.
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

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/vm/native-data.h"

#undef TRACE

#include "../../../mongodb.h"
#include "../../../utils.h"

#include "ReadPreference.h"

namespace HPHP {

const StaticString s_MongoDriverReadPreference_className("MongoDB\\Driver\\ReadPreference");
Class* MongoDBDriverReadPreferenceData::s_class = nullptr;
const StaticString MongoDBDriverReadPreferenceData::s_className("MongoDBDriverReadPreference");
IMPLEMENT_GET_CLASS(MongoDBDriverReadPreferenceData);

void HHVM_METHOD(MongoDBDriverReadPreference, _setReadPreference, int readPreference)
{
	MongoDBDriverReadPreferenceData* data = Native::data<MongoDBDriverReadPreferenceData>(this_);

	data->m_read_preference = mongoc_read_prefs_new((mongoc_read_mode_t) readPreference);
}

void HHVM_METHOD(MongoDBDriverReadPreference, _setReadPreferenceTags, const Array &tagSets)
{
	MongoDBDriverReadPreferenceData* data = Native::data<MongoDBDriverReadPreferenceData>(this_);
	bson_t *bson;

	/* Convert argument */
	VariantToBsonConverter converter(tagSets, HIPPO_BSON_NO_FLAGS);
	bson = bson_new();
	converter.convert(bson);

	/* Set and check errors */
	mongoc_read_prefs_set_tags(data->m_read_preference, bson);
	bson_destroy(bson);
	if (!mongoc_read_prefs_is_valid(data->m_read_preference)) {
		/* Throw exception */
		throw MongoDriver::Utils::throwInvalidArgumentException("Invalid tagSets");
	}
}

const StaticString
	s_mode("mode"),
	s_tags("tags");


Array HHVM_METHOD(MongoDBDriverReadPreference, __debugInfo)
{
	MongoDBDriverReadPreferenceData* data = Native::data<MongoDBDriverReadPreferenceData>(this_);
	Array retval = Array::Create();
	Variant v_tags;
	const bson_t *tags = mongoc_read_prefs_get_tags(data->m_read_preference);

	retval.set(s_mode, mongoc_read_prefs_get_mode(data->m_read_preference));

	hippo_bson_conversion_options_t options = HIPPO_TYPEMAP_DEBUG_INITIALIZER;
	BsonToVariantConverter convertor(bson_get_data(tags), tags->len, options);
	convertor.convert(&v_tags);
	retval.set(s_tags, v_tags.toArray());

	return retval;
}

int64_t HHVM_METHOD(MongoDBDriverReadPreference, getMode)
{
	MongoDBDriverReadPreferenceData* data = Native::data<MongoDBDriverReadPreferenceData>(this_);

	return mongoc_read_prefs_get_mode(data->m_read_preference);
}

Array HHVM_METHOD(MongoDBDriverReadPreference, getTagSets)
{
	MongoDBDriverReadPreferenceData* data = Native::data<MongoDBDriverReadPreferenceData>(this_);
	Variant v_tags;
	const bson_t *tags = mongoc_read_prefs_get_tags(data->m_read_preference);
	
	hippo_bson_conversion_options_t options = HIPPO_TYPEMAP_DEBUG_INITIALIZER;
	BsonToVariantConverter convertor(bson_get_data(tags), tags->len, options);
	convertor.convert(&v_tags);

	return v_tags.toArray();
}

}
