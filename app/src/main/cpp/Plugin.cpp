#include "Plugin.h"
using namespace nlohmann ;

void Plugin::free () {
    descriptor->cleanup (handle);
}

Plugin::Plugin (const LADSPA_Descriptor * _descriptor, unsigned long _sampleRate, SharedLibrary::PluginType _type) {
    type = _type;
    if (_descriptor == NULL) {
        LOGF ("[%s:%s] null descriptor passed", __FILE__, __PRETTY_FUNCTION__ );
        return ;
    }

    descriptor = _descriptor ;
    if (_sampleRate > 0)
        sampleRate = _sampleRate ;
    else {
        LOGF ("[%s: %s] 0 sample rate passed", __FILE__, __PRETTY_FUNCTION__ );
        sampleRate = 48000 ;
    }

    if (type == SharedLibrary::LADSPA) {
        LOGD("Creating plugin: %s", _descriptor->Name);

        handle = (LADSPA_Handle *) descriptor->instantiate(descriptor, sampleRate);
        if (descriptor->activate) {
            descriptor->activate(handle);
        }

        LOGD("[%s] loaded plugin %s [%d: %s] at %u", __PRETTY_FUNCTION__, descriptor->Name,
             descriptor->UniqueID, descriptor->Label, sampleRate);
        print();

        for (int i = 0; i < descriptor->PortCount; i++) {
            LADSPA_PortDescriptor port = descriptor->PortDescriptors[i];
            if (LADSPA_IS_PORT_AUDIO(port)) {
                if (LADSPA_IS_PORT_INPUT(port)) {
                    LOGD("[%s %d]: found input port", descriptor->Name, i);
                    if (inputPort == -1)
                        inputPort = i;
                    else if (inputPort2 == -1)
                        inputPort2 = i;
                    else
                        LOGE("[%s %d]: %s is third input port", descriptor->Name, i,
                             descriptor->PortNames[i]);
                } else if (LADSPA_IS_PORT_OUTPUT(port)) {
                    LOGD("[%s %d]: found output port", descriptor->Name, i);
                    if (outputPort == -1)
                        outputPort = i;
                    else if (outputPort2 == -1)
                        outputPort2 = i;
                    else
                        LOGE("[%s %d]: %s is third output port", descriptor->Name, i,
                             descriptor->PortNames[i]);

                }
            } else if (/*LADSPA_IS_PORT_OUTPUT(port)*/ false) {
                LOGE("[%s:%d] %s: ladspa port is output but not audio!", descriptor->Name, i,
                     descriptor->PortNames[i]);
                // this, erm, doesn't work
                /*
                if (outputPort == -1)
                    outputPort = port ;
                */
            } else if (LADSPA_IS_PORT_CONTROL(port) && LADSPA_IS_PORT_INPUT(port)) {
                LOGD("[%s %d]: found control port", descriptor->Name, i);
                PluginControl *pluginControl = new PluginControl(descriptor, i);
                descriptor->connect_port(handle, i, pluginControl->def);
                pluginControls.push_back(pluginControl);
            } else if (LADSPA_IS_PORT_CONTROL(port) && LADSPA_IS_PORT_OUTPUT(port)) {
                LOGD("[%s %d]: found possible monitor port", descriptor->Name, i);
                descriptor->connect_port(handle, i, &dummy_output_control_port);
            } else {
                // special case, aaaargh!
                if (descriptor->UniqueID == 2606) {
                    if (i == 2)
                        inputPort = i;
                    if (i == 3)
                        outputPort = i;
                    if (i == 0 || i == 1) {
                        PluginControl *pluginControl = new PluginControl(descriptor, i);
                        descriptor->connect_port(handle, i, pluginControl->def);
                        pluginControls.push_back(pluginControl);

                        if (i == 0) {
                            pluginControl->min = 0;
                            pluginControl->max = 25;
                        } else if (i == 1) {
                            pluginControl->min = -24;
                            pluginControl->max = 24;
                        }
                    }
                } else {
                    LOGE("[%s %d]: unknown port %s for %s (%d)", descriptor->Name, i,
                         descriptor->PortNames[i], descriptor->Label, descriptor->UniqueID);
                    descriptor->connect_port(handle, i, &dummy_output_control_port);
                }
            }
        }
    } else if (type == SharedLibrary::LV2) {
        LOGD("[LV2] waiting for shared library pointer ...") ;
        lv2Descriptor = (LV2_Descriptor *) descriptor ;

    }
}

void Plugin::print () {
    LOGD("--------| Controls for %s: %d |--------------", descriptor->Name, descriptor ->PortCount) ;
    for (int i = 0 ; i < pluginControls.size() ; i ++) {
        pluginControls.at(i)->print();
    }
}

void Plugin::load () {
    LOGD("Creating plugin: %s from %s @ %s", lv2Descriptor->URI, sharedLibrary->LIBRARY_PATH.c_str(), sharedLibrary->so_file.c_str());
    std::string lib_path = sharedLibrary->LIBRARY_PATH + "/" + sharedLibrary -> so_file + ".lv2/" ;
    LOGD("[LV2] library path: %s", lib_path.c_str());

    handle = (LADSPA_Handle *) lv2Descriptor->instantiate(lv2Descriptor, sampleRate, lib_path.c_str(), sharedLibrary->feature_list);
    LOGD("[LV2] Handle instantiated ok! Congratulations");

    std::string json_ = getLV2JSON(lv2Descriptor -> URI);
    json j = json::parse(json_);
    LOGD("[LV2 JSON] %s", std::string (j ["1"]["name"]).c_str());

    recursive_iterate(*this, j, [this, &j](Plugin *plugin, json::const_iterator it){
//        std::cout << *it << std::endl;
        LADSPA_PortDescriptor port = j[it]["index"];
        if (j [it].is_object ("AudioPort")) {
            if (j [it].is_object ("InputPort")) {
                LOGD("[%s %d]: found input port",plugin->sharedLibrary->so_file.c_str(), port);
                if (plugin-> inputPort == -1)
                    plugin->inputPort = port;
                else if (plugin->inputPort2 == -1)
                    plugin->inputPort2 = port;
                else
                    LOGE("[%s %d]: %s is third input port", plugin->sharedLibrary->so_file.c_str(), port, j [it]["name"]);
            } else if (LADSPA_IS_PORT_OUTPUT(port)) {
                LOGD("[%s %d]: found output port", plugin->sharedLibrary->so_file.c_str(), port);
                if (plugin->outputPort == -1)
                    plugin->outputPort = port;
                else if (plugin ->outputPort2 == -1)
                    plugin->outputPort2 = port;
                else
                    LOGE("[%s %d]: %s is third output port", plugin->sharedLibrary->so_file.c_str(), port,
                         j[it]["name"]);

            }
        } else if (/*LADSPA_IS_PORT_OUTPUT(port)*/ false) {
            LOGE("[%s:%d] %s: ladspa port is output but not audio!", lv2Descriptor->URI, port,
                 j["name"]);
            // this, erm, doesn't work
            /*
            if (outputPort == -1)
                outputPort = port ;
            */
        } else if (j [it].is_object ("InputPort") && j [it].is_object ("ControlPort")) {
            LOGD("[%s %d]: found control port", plugin->sharedLibrary->so_file.c_str(), port);
            PluginControl *pluginControl = new PluginControl(lv2Descriptor, j);
            lv2Descriptor->connect_port(handle, port, pluginControl->def);
            pluginControls.push_back(pluginControl);
        } else if (j [it].is_object ("OutputPort") && j [it].is_object ("ControlPort")) {
            LOGD("[%s %d]: found possible monitor port", lv2Descriptor->URI, port);
            lv2Descriptor->connect_port(handle, port, &dummy_output_control_port);
        } else {
            // special case, aaaargh!
            if (descriptor->UniqueID == 2606) {
                if (i == 2)
                    inputPort = i;
                if (i == 3)
                    outputPort = i;
                if (i == 0 || i == 1) {
                    PluginControl *pluginControl = new PluginControl(descriptor, i);
                    descriptor->connect_port(handle, i, pluginControl->def);
                    pluginControls.push_back(pluginControl);

                    if (i == 0) {
                        pluginControl->min = 0;
                        pluginControl->max = 25;
                    } else if (i == 1) {
                        pluginControl->min = -24;
                        pluginControl->max = 24;
                    }
                }
            } else {
                LOGE("[%s %d]: unknown port %s for %s (%d)", descriptor->Name, i,
                     descriptor->PortNames[i], descriptor->Label, descriptor->UniqueID);
                descriptor->connect_port(handle, i, &dummy_output_control_port);
            }
        }
    });

}

std::string Plugin::getLV2JSON (std::string pluginName) {
    JNIEnv *env;
    sharedLibrary -> vm-> GetEnv((void**)&env, JNI_VERSION_1_6);
    if (env == NULL) {
        LOGF("cannot find env!");
    }

    jstring jstr1 = env->NewStringUTF(pluginName.c_str());
    jstring libname = env->NewStringUTF(sharedLibrary->so_file.c_str());

    jclass clazz = env->FindClass("com/shajikhan/ladspa/amprack/MainActivity");
    if (clazz == nullptr) {
        LOGF("cannot find class!");
    }

    jmethodID mid = env->GetStaticMethodID(clazz, "getLV2Info",
                                           "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (mid == nullptr) {
        LOGF("cannot find method!");
    }

    jobject obj = env->CallStaticObjectMethod(clazz, mid, libname, jstr1);
    if (obj == nullptr) {
        LOGF("cannot find class!");
    }

    jstring retStr = (jstring)obj;
    const char *nativeString = env->GetStringUTFChars(retStr, 0);
    std::string str = std::string (nativeString);
    env->ReleaseStringUTFChars(retStr, nativeString);


    return str;
}
