
// This file is part of node-lmdb, the Node.js binding for lmdb
// Copyright (c) 2013 Timur Kristóf
// Licensed to you under the terms of the MIT license
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "node-lmdb.h"
#include <string.h>
#include <stdio.h>

void setupExportMisc(Handle<Object> exports) {
    Local<Object> versionObj = Nan::New<Object>();

    int major, minor, patch;
    char *str = mdb_version(&major, &minor, &patch);
    versionObj->Set(Nan::New<String>("versionString").ToLocalChecked(), Nan::New<String>(str).ToLocalChecked());
    versionObj->Set(Nan::New<String>("major").ToLocalChecked(), Nan::New<Integer>(major));
    versionObj->Set(Nan::New<String>("minor").ToLocalChecked(), Nan::New<Integer>(minor));
    versionObj->Set(Nan::New<String>("patch").ToLocalChecked(), Nan::New<Integer>(patch));

    exports->Set(Nan::New<String>("version").ToLocalChecked(), versionObj);
}

void setFlagFromValue(int *flags, int flag, const char *name, bool defaultValue, Local<Object> options) {
    Local<Value> opt = options->Get(Nan::New<String>(name).ToLocalChecked());
    if (opt->IsBoolean() ? opt->BooleanValue() : defaultValue) {
        *flags |= flag;
    }
}

argtokey_callback_t argToKey(const Local<Value> &val, MDB_val &key, bool keyIsUint32) {
    // Check key type
    if (keyIsUint32 && !val->IsUint32()) {
        Nan::ThrowError("Invalid key. keyIsUint32 specified on the database, but the given key was not an unsigned 32-bit integer");
        return nullptr;
    }
    if (!keyIsUint32 && !val->IsString()) {
        Nan::ThrowError("Invalid key. String key expected, because keyIsUint32 isn't specified on the database.");
        return nullptr;
    }

    // Handle uint32_t key
    if (keyIsUint32) {
        uint32_t *v = new uint32_t;
        *v = val->Uint32Value();

        key.mv_size = sizeof(uint32_t);
        key.mv_data = v;

        return ([](MDB_val &key) -> void {
            delete (uint32_t*)key.mv_data;
        });
    }

    // Handle string key
    CustomExternalStringResource::writeTo(val->ToString(), &key);
    return ([](MDB_val &key) -> void {
        delete[] (uint16_t*)key.mv_data;
    });

    return nullptr;
}

Local<Value> keyToHandle(MDB_val &key, bool keyIsUint32) {
    if (keyIsUint32) {
        return Nan::New<Integer>(*((uint32_t*)key.mv_data));
    }
    else {
        return valToString(key);
    }
}

Local<Value> valToString(MDB_val &data) {
    auto resource = new CustomExternalStringResource(&data);
    auto str = Nan::New<v8::String>(resource);

    return str.ToLocalChecked();
}

Local<Value> valToBinary(MDB_val &data) {
    // FIXME: It'd be better not to copy buffers, but I'm not sure
    // about the ownership semantics of MDB_val, so let' be safe.
    return Nan::CopyBuffer(
        (char*)data.mv_data,
        data.mv_size
    ).ToLocalChecked();
}

Local<Value> valToNumber(MDB_val &data) {
    return Nan::New<Number>(*((double*)data.mv_data));
}

Local<Value> valToBoolean(MDB_val &data) {
    return Nan::New<Boolean>(*((bool*)data.mv_data));
}

void consoleLog(const char *msg) {
    Local<String> str = Nan::New("console.log('").ToLocalChecked();
    str = String::Concat(str, Nan::New<String>(msg).ToLocalChecked());
    str = String::Concat(str, Nan::New("');").ToLocalChecked());

    Local<Script> script = Nan::CompileScript(str).ToLocalChecked();
    Nan::RunScript(script);
}

void consoleLog(Local<Value> val) {
    Local<String> str = Nan::New<String>("console.log('").ToLocalChecked();
    str = String::Concat(str, val->ToString());
    str = String::Concat(str, Nan::New<String>("');").ToLocalChecked());

    Local<Script> script = Nan::CompileScript(str).ToLocalChecked();
    Nan::RunScript(script);
}

void consoleLogN(int n) {
    char c[20];
    memset(c, 0, 20 * sizeof(char));
    sprintf(c, "%d", n);
    consoleLog(c);
}

/*
Writing string to LMDB.
Making LMDB compatible with other languages by converting strings to UTF-8.
Alas, this means no zero-copy semantics because strings are UTF-16 in Javascript.
*/
void CustomExternalStringResource::writeTo(Handle<String> str, MDB_val *val) {
    String::Utf8Value utf8(str);
    char *inp = *utf8;
    int len = strlen(inp);

    char *d = new char[len];
    strcpy(d, inp);

    val->mv_data = d;
    val->mv_size = len;
}

/*
Reading string from LMDB.
It will be utf-8 encoded, so decoding here into utf-16.
*/
CustomExternalStringResource::CustomExternalStringResource(MDB_val *val) {
    // appending '\0' to input to feed into String::New
    char *tmp = new char[val->mv_size + 1];
    memcpy(tmp, val->mv_data, val->mv_size);
    tmp[val->mv_size] = 0;

    // convert utf8 -> utf16
    Local<String> jsString = String::New(tmp);
    delete [] tmp;

    // write into output buffer
    int len = jsString->Length();
    uint16_t *d = new uint16_t[len];
    jsString->Write(d);
    this->d = d;
    this->l = len;
}

CustomExternalStringResource::~CustomExternalStringResource() {
    // TODO: alter this if zero-copy semantics is reintroduced above
    delete [] d;
}

void CustomExternalStringResource::Dispose() {
    // No need to do anything, the data is owned by LMDB, not us

    // But actually need to delete the string resource itself:
    // the docs say that "The default implementation will use the delete operator."
    // while initially I thought this means using delete on the string,
    // apparently they meant just calling the destructor of this class.
    delete this;
}

const uint16_t *CustomExternalStringResource::data() const {
    return this->d;
}

size_t CustomExternalStringResource::length() const {
    return this->l;
}
