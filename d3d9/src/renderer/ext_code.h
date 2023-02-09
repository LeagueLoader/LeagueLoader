
/*
    window.openDevTools()
        // Open DevTools of League Client.

    window.require(name: String) : any
        // Simple implementation of CommonJS "require".

    window.openPluginsFolder()
        // Open the plugins folder.
*/

u8R"===============(

var DataStore;
var Effect;
var openDevTools;
var openAssetsFolder;
var openPluginsFolder;
var require;

(function() {

    native function LoadData();
    native function SaveData();

    let _data;
    try {
        const object = JSON.parse(LoadData());
        _data = new Map(Object.entries(object));
    } catch {
        _data = new Map();
    }

    DataStore = {
        has(key) {
            return _data.has(key);
        },
        get(key) {
            return _data.get(key);
        },
        async set(key, value) {
            _data.set(key, value);
            const object = Object.fromEntries(_data);
            SaveData(JSON.stringify(object));
        },
        remove(key) {
            return _data.delete(key);
        }
    };

    Effect = {
        get current() {
            native function GetEffect();
            return GetEffect() || undefined;
        },
        apply(name, options = undefined) {
            native function ApplyEffect();
            return ApplyEffect(name, options);
        },
        clear() {
            native function ClearEffect();
            ClearEffect();
        }
    };

    openDevTools = function () {
        native function OpenDevTools();
        OpenDevTools();
    };

    openAssetsFolder = function () {
        native function OpenAssetsFolder();
        OpenAssetsFolder();
    };

    openPluginsFolder = function () {
        native function OpenPluginsFolder();
        OpenPluginsFolder();
    };

    var join = function (a, b) {
        var parts = a.split("/").concat(b.split("/"));
        var newParts = [];
          
        for (var i = 0, l = parts.length; i < l; i++) {
            var part = parts[i];
            if (!part || part === ".") continue;
            if (part === "..") newParts.pop();
            else newParts.push(part);
        }

        if (parts[0] === "") newParts.unshift("");
        return newParts.join("/") || (newParts.length ? "/" : ".");
    };

    var global = {};    // Global object.
    var modules = {};   // Modules map.
    var paths = [''];   // Path stack.

    require = function (name) {
        native function Require();
            
        // Check invalid name.
        if (typeof name !== "string" || name === "") {
            throw Error("Module name is required.")
        }
            
        // Get current dir.
        var dir = paths[paths.length - 1];
        var path = join(dir, name);
        var mod = modules[path];
            
        if (typeof mod === "object") {
            // Found exported.
            return mod.exports;
        } else {
            // Require path.
            var data = Require(path);
                
            if (data === null) {
                throw Error("Cannot find module '" + name + "'");
            } else {
                var source = data[0];
                var type = data[1];

                var _M = {};
                    
                // Textual
                if (type === 2) {
                    _M.source = source;
                    // JSON
                    if (/\.json$/i.test(name)) {
                        _M.exports = JSON.parse(source);
                    } else {
                        _M.exports = source;
                    }
                } else {
                    // Default exports.
                    _M.exports = {};
                    // Fake source map URL.
                    _M.source = source + "\n//# sourceURL=@plugins" + path +
                        (type === 1 ? "/index" : ".js");

                    // Create function.
                    var code = "\"use strict\";eval(module.source);";
                    var exec = Function("module", "exports", "require", "global", code);

                    try {
                        // Push current dir.
                        paths.push(join(path, ".."));
                        // Execute.
                        exec(_M, _M.exports, require, global);
                    } catch (err) {
                        throw err;
                    } finally {
                        paths.pop();
                    }
                        
                    if (type === 1) {
                        modules[path + "/index"] = _M;
                    }
                }

                modules[path] = _M;
                return _M.exports;
            }
        }
    };
})();

)==============="