/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * class_builder.hpp
 */

#ifndef _CLASS_BUILDER_HPP_
#define _CLASS_BUILDER_HPP_

#include <string>

#include "schema_system_catalog_definition.hpp"

namespace cubschema {
        class attribute_type_builder {
                public:
                void set_name (const std::string &name);
                void set_type (const std::string &type);
                void set_default_value (const DB_VALUE& val);

                attribute_type build ();
        };

        class attribute_type {

        };

        class class_type {

        };

        class class_type_builder {
                using attr_vec_type = std::vector <attribute>;

                public:
                        void add (const std::string& name, const std::string& type);
                        void add_all (const attr_vec_type& attrs);

                        class_type build ();

                private:
                        std::string name;
                        std::vector <attribute> attributes;

        };

        class class_builder {

                public:
                        explicit class_builder (const class_type& ctype);
        };
}

#endif // _CLASS_BUILDER_HPP_
