/*
 * Copyright 2016 CodiLime
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef VELES_PARSER_UNPYC_H
#define VELES_PARSER_UNPYC_H

#include "data/bindata.h"
#include "dbif/types.h"
#include "parser/parser.h"

namespace veles {
namespace parser {

void unpycFileBlob(veles::dbif::ObjectHandle blob, uint64_t start = 0);

class PycParser : public Parser {
 public:
  PycParser()
      : Parser("pyc3", {data::BinData(8, {0x9e, 0x0c, '\r', '\n'}),
                        data::BinData(8, {0xee, 0x0c, '\r', '\n'}),
                        data::BinData(8, {0x16, 0x0c, '\r', '\n'})}) {}
  void parse(dbif::ObjectHandle blob, uint64_t start = 0) override {
    unpycFileBlob(blob, start);
  }
};

}  // namespace parser
}  // namespace veles

#endif
