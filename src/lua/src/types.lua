---@meta
-- The types of the ms API of Mikser Studio's Lua plugins (docs/plugins.md), for Lua language
-- servers such as LuaLS. mikser-studio --print-lua-types writes this file; don't edit it.

---@alias ms.permission {permissions}

---@alias ms.error_code {error_codes}

---A command's failure, as ms.run, ms.transaction and job:await raise it.
---@class ms.error
---@field code ms.error_code
---@field message string

---Who runs a command.
---@class ms.context
---@field origin string "ui", "api" or "plugin:<id>": who started the run.
---@field origins string[] The chain of callers, the one who started the run first.

---@class ms.command_spec
---@field id string Starts with the plugin's id and a dot.
---@field title string
---@field handler fun(params: table, ctx: ms.context): table? Returns the result, or raises an error.
---@field description? string
---@field category? string The menu group; the plugin's name by default.
---@field params? table The JSON Schema of the params.
---@field result? table The JSON Schema of the result.
---@field permission? ms.permission What a caller needs to run it.
---@field read_only? boolean
---@field confirmation? string A question the person answers before each run.
---@field open_world? boolean It reaches outside the app.
---@field starts_job? boolean Its work goes on in a job that the result names.
---@field quiet? boolean The command log and the status bar leave it out.
---@field shortcut? string

---A job: one this plugin started, or one ms.jobs.get names.
---@class ms.job
---@field id string
local job = {}

---Waits for the job to end and returns its result; raises an ms.error when it failed or was canceled.
---@return table
function job:await() end

---Cancels the job.
function job:cancel() end

---Shows how far the job has come, from 0 to 1, and what it does now. Only on the job that
---ms.jobs.run gives its function.
---@param fraction number
---@param message? string
function job:progress(fraction, message) end

---What ms.jobs.run starts.
---@class ms.job_spec
---@field kind string Lower-case words joined by dots; the job's kind is "<plugin id>.<kind>".
---@field title string What the person sees, such as "Generate text: a red fox".
---@field lane? string Jobs of one lane run one at a time, in the order they came.

---@class ms.rational
---@field num integer
---@field den integer

---@class ms.media_info
---@field kind string
---@field durationFrames integer
---@field width integer
---@field height integer
---@field frameRate ms.rational
---@field hasVideo boolean
---@field hasAudio boolean
---@field audioSampleRate integer
---@field audioChannels integer

---@class ms.media
---@field id string
---@field path string
---@field name string
---@field info ms.media_info
---@field provenance table

---@class ms.track
---@field id string
---@field kind "video"|"audio"
---@field name string
---@field muted boolean
---@field hidden boolean

---@class ms.clip
---@field id string
---@field mediaId string
---@field trackId string
---@field position integer The clip's first frame on the timeline.
---@field in integer The first frame of the media it shows.
---@field out integer The frame after the last one it shows.

---The project as it was when ms.project() was called, read-only.
---@class ms.project
---@field session string
---@field revision integer
---@field frameRate ms.rational
---@field width integer
---@field height integer
---@field media ms.media[]
---@field tracks ms.track[]
---@field clips ms.clip[]
local project = {}

---@param id string
---@return ms.media?
function project:find_media(id) end

---@param id string
---@return ms.track?
function project:find_track(id) end

---@param id string
---@return ms.clip?
function project:find_clip(id) end

---The clips that overlap the frames [from, to), on one track or on all.
---@param from integer
---@param to integer
---@param track_id? string
---@return ms.clip[]
function project:clips_in_range(from, to, track_id) end

---@param media_id string
---@return ms.clip[]
function project:clips_of_media(media_id) end

ms = {}

---The plugin's id.
---@type string
ms.plugin_id = ""

---JSON's null inside an array.
---@type lightuserdata
ms.null = nil

---Registers a command.
---@param spec ms.command_spec
function ms.command(spec) end

---Runs a command and waits for its result; raises an ms.error when it fails.
---@param id string
---@param params? table
---@return table
function ms.run(id, params) end

---Calls the callback for each event of a topic, such as "clip.added", or of a family, such as "clip.*".
---@param topic string
---@param callback fun(payload: table, topic: string)
function ms.on(topic, callback) end

---Runs fn; the commands it runs make one undo step called name. An error in fn undoes them all and is
---raised again.
---@param name string
---@param fn fun()
function ms.transaction(name, fn) end

ms.jobs = {}

---Starts a job of a kind the app provides.
---@param kind string
---@param input? table
---@return ms.job
function ms.jobs.start(kind, input) end

---Starts a job of the plugin's own, whose work `fn` does: its return value is the job's result, and
---an error it raises fails the job. The jobs it starts are part of this one.
---@param spec ms.job_spec
---@param fn fun(job: ms.job): table?
---@return ms.job
function ms.jobs.run(spec, fn) end

---The job with this id, to await or cancel.
---@param id string
---@return ms.job
function ms.jobs.get(id) end

---The project as it is now, read-only.
---@return ms.project
function ms.project() end

---The plugin's settings, with the schema's defaults.
---@return table
function ms.settings() end

---Marks a table as a JSON array, so that an empty one stays [].
---@generic T: table
---@param t? T
---@return T
function ms.array(t) end

ms.log = {}

---@param ... any
function ms.log.debug(...) end

---@param ... any
function ms.log.info(...) end

---@param ... any
function ms.log.warning(...) end

---@param ... any
function ms.log.error(...) end
