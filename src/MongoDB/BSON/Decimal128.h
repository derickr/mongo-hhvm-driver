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
#ifdef BSON_EXPERIMENTAL_FEATURES

#ifndef __MONGODB_BSON_DECIMAL_H__
#define __MONGODB_BSON_DECIMAL_H__
namespace HPHP {

extern "C" {
#include "../../../libbson/src/bson/bson.h"
}

extern const StaticString s_MongoBsonDecimal128_className;

class MongoDBBsonDecimal128Data
{
	public:
		static Class* s_class;
		static const StaticString s_className;

		static Class* getClass();

		bson_decimal128_t m_decimal;

		void sweep() {
		}

		~MongoDBBsonDecimal128Data() {
			sweep();
		};
};

void HHVM_METHOD(MongoDBBsonDecimal128, __construct, const String &decimal);
String HHVM_METHOD(MongoDBBsonDecimal128, __toString);
Array HHVM_METHOD(MongoDBBsonDecimal128, __debugInfo);
}
#endif

#endif /* BSON_EXPERIMENTAL_FEATURES */
