-- The Lua half of the ms API, run once in each plugin's state before the plugin's code.
--
-- Everything that may wait is written here, in Lua: a C++ function that yields fails, and sol2
-- has no continuations. Command handlers, event callbacks and the plugin's main chunk each run
-- in a coroutine of their own, a task. ms.run and the other calls that wait post a request to the
-- main thread through `host` and yield; the host resumes the task with the answer.
--
-- The host calls the functions this chunk returns; the plugin sees only the global `ms` and the
-- sandboxed standard library.

local host, traceback, raw_load = ...

local PENDING = {} -- What a task yields while it waits for the host.
local tasks = setmetatable({}, { __mode = "k" }) -- coroutine -> task
local waiting = {} -- call id -> coroutine
local next_call = 0
local handlers = {} -- topic -> list of callbacks
local job_waiters = {} -- job id -> list of call ids
local finished_jobs = {} -- job id -> the job.finished payload, for jobs this plugin started
local started_jobs = {} -- job id -> true while this plugin waits to hear how it ended
local early_jobs, early_order = {}, {} -- jobs that ended before ms.jobs.start heard their id
local command_handlers = {} -- handler id -> function

local array_mt = host.array_mt
local error_mt = {
	__name = "ms.error",
	__tostring = function(e)
		return e.message
	end,
}

local function new_error(code, message)
	return setmetatable({ code = code, message = message }, error_mt)
end

-- The person's message for an error value, and its code.
local function describe(value)
	if getmetatable(value) == error_mt then
		return value.code, value.message
	end
	return "failed", tostring(value)
end

-- The message with the stack of `co`, when there is one.
local function described(co, message)
	return co and traceback(co, message) or message
end

local function finish(task, ok, value, co)
	if task.kind == "command" then
		if ok then
			host.done(task.run, true, type(value) == "table" and value or {})
		else
			local code, message = describe(value)
			host.log("error", described(co, message))
			host.done(task.run, false, { code = code, message = message })
		end
	elseif task.kind == "main" then
		if ok then
			host.started(true)
		else
			local _, message = describe(value)
			host.log("error", described(co, message))
			host.started(false, message)
		end
	elseif not ok then
		local _, message = describe(value)
		host.log("error", described(co, message))
	end
end

-- Runs a task until it waits, ends or fails.
local function step(co, ...)
	local task = tasks[co]
	local ok, first, second = coroutine.resume(co, ...)
	if not ok then
		tasks[co] = nil
		if first == "not enough memory" then
			-- The failed task's stack holds what filled the memory: let it go before answering.
			co = nil
			collectgarbage()
		end
		finish(task, false, first, co)
	elseif coroutine.status(co) == "dead" then
		tasks[co] = nil
		finish(task, true, first, co)
	elseif first ~= PENDING then
		tasks[co] = nil
		finish(task, false, new_error("failed", "A plugin's code yielded outside ms.run; it can't be resumed."), co)
	else
		waiting[second] = co
	end
end

local function spawn(kind, run, fn, ...)
	local co = coroutine.create(fn)
	tasks[co] = { kind = kind, run = run }
	return step(co, ...) -- A tail call, so that only the task holds `co`.
end

local function current_task(what)
	local co, main = coroutine.running()
	local task = tasks[co]
	if main or not task or not coroutine.isyieldable() then
		error(what .. " works only in a command handler, an event callback or the plugin's main code", 3)
	end
	return task
end

-- Posts a request through `send(call_id)` and waits for its answer.
local function wait(task, send)
	next_call = next_call + 1
	local call = next_call
	local sent, problem = send(call, task.run)
	if sent == false then
		error(new_error("invalid_params", problem), 3)
	end
	local ok, value = coroutine.yield(PENDING, call)
	if ok then
		return value
	end
	error(setmetatable(value, error_mt), 3)
end

local dispatch = {}

function dispatch.answer(call, ok, value)
	local co = waiting[call]
	if co then
		waiting[call] = nil
		return step(co, ok, value)
	end
end

function dispatch.invoke(run, handler_id, params, context)
	local handler = command_handlers[handler_id]
	if not handler then
		host.done(run, false, { code = "not_available", message = "The command's handler is gone." })
		return
	end
	spawn("command", run, handler, params, context)
end

function dispatch.main(source, chunk_name)
	local chunk, problem = raw_load(source, chunk_name, "t")
	if not chunk then
		host.started(false, problem)
		return
	end
	spawn("main", 0, chunk)
end

local function job_ended(payload)
	local id = payload.jobId
	if not started_jobs[id] then
		-- It may be one of ours whose start hasn't been answered yet.
		early_jobs[id] = payload
		table.insert(early_order, id)
		if #early_order > 32 then
			early_jobs[table.remove(early_order, 1)] = nil
		end
		return
	end
	started_jobs[id] = nil
	local waiters = job_waiters[id]
	job_waiters[id] = nil
	if not waiters then
		finished_jobs[id] = payload
		return
	end
	for _, call in ipairs(waiters) do
		if payload.state == "succeeded" then
			dispatch.answer(call, true, payload.result or {})
		else
			local code = payload.state == "canceled" and "canceled" or "failed"
			dispatch.answer(call, false, { code = code, message = payload.error or code })
		end
	end
end

function dispatch.event(topic, payload)
	if topic == "job.finished" then
		job_ended(payload)
	end
	for _, callback in ipairs(handlers[topic] or {}) do
		spawn("event", 0, callback, payload, topic)
	end
	local family = topic:match("^(.-)%.[^.]*$")
	if family then
		for _, callback in ipairs(handlers[family .. ".*"] or {}) do
			spawn("event", 0, callback, payload, topic)
		end
	end
end

-- The plugin's API --------------------------------------------------------------------------

ms = {}
ms.null = host.null
ms.plugin_id = host.plugin_id

-- A table that becomes a JSON array even when it is empty.
function ms.array(t)
	return setmetatable(t or {}, array_mt)
end

function ms.command(spec)
	if type(spec) ~= "table" or type(spec.handler) ~= "function" then
		error("ms.command needs a table with an id, a title and a handler function", 2)
	end
	local handler = spec.handler
	local fields = {}
	for key, value in pairs(spec) do
		if key ~= "handler" then
			fields[key] = value
		end
	end
	local handler_id = #command_handlers + 1
	command_handlers[handler_id] = handler
	local ok, problem = host.register(fields, handler_id)
	if not ok then
		command_handlers[handler_id] = nil
		error(problem, 2)
	end
end

function ms.run(id, params)
	local task = current_task("ms.run")
	return wait(task, function(call, run)
		return host.run(call, run, id, params or {})
	end)
end

function ms.on(topic, callback)
	if type(topic) ~= "string" or type(callback) ~= "function" then
		error("ms.on needs a topic and a callback function", 2)
	end
	if not handlers[topic] then
		handlers[topic] = {}
		host.subscribe(topic)
	end
	table.insert(handlers[topic], callback)
end

function ms.transaction(name, fn)
	local task = current_task("ms.transaction")
	if task.transaction then
		error("ms.transaction can't open a transaction inside another", 2)
	end
	local transaction = wait(task, function(call, run)
		return host.begin(call, run, name)
	end)
	task.transaction = transaction
	local ok, problem = pcall(fn)
	task.transaction = nil
	if ok then
		wait(task, function(call)
			return host.finish(call, transaction, true)
		end)
		return
	end
	local _, message = describe(problem)
	wait(task, function(call)
		return host.finish(call, transaction, false, message)
	end)
	error(problem, 0)
end

local job_mt = {}
job_mt.__index = job_mt

function job_mt:await()
	local task = current_task("job:await")
	if task.transaction then
		error("A job can't be awaited inside ms.transaction: it would hold the project for its whole run", 2)
	end
	local ended = finished_jobs[self.id]
	if ended then
		finished_jobs[self.id] = nil
		if ended.state == "succeeded" then
			return ended.result or {}
		end
		local code = ended.state == "canceled" and "canceled" or "failed"
		error(new_error(code, ended.error or code), 2)
	end
	return wait(task, function(call)
		job_waiters[self.id] = job_waiters[self.id] or {}
		table.insert(job_waiters[self.id], call)
	end)
end

function job_mt:cancel()
	return ms.run("jobs.cancel", { jobId = self.id })
end

ms.jobs = {}

function ms.jobs.start(kind, input)
	local task = current_task("ms.jobs.start")
	local id = wait(task, function(call, run)
		return host.start_job(call, run, kind, input or {})
	end)
	if early_jobs[id] then
		finished_jobs[id] = early_jobs[id]
		early_jobs[id] = nil
	else
		started_jobs[id] = true
	end
	return setmetatable({ id = id }, job_mt)
end

function ms.settings()
	return host.settings()
end

ms.log = {}
for _, level in ipairs({ "debug", "info", "warning", "error" }) do
	ms.log[level] = function(...)
		local parts = table.pack(...)
		for i = 1, parts.n do
			parts[i] = tostring(parts[i])
		end
		host.log(level, table.concat(parts, " ", 1, parts.n))
	end
end
print = ms.log.info

-- The project, read-only ---------------------------------------------------------------------

local function read_only(data)
	local proxy = {}
	return setmetatable(proxy, {
		__index = function(_, key)
			local value = data[key]
			if type(value) == "table" then
				value = read_only(value)
			end
			return value
		end,
		__newindex = function()
			error("The project view is read-only; change the project with ms.run", 2)
		end,
		__len = function()
			return #data
		end,
		__pairs = function()
			local key
			return function()
				local value
				key, value = next(data, key)
				if type(value) == "table" then
					value = read_only(value)
				end
				return key, value
			end
		end,
	})
end

local view_mt = {}

local function find(list, id)
	for _, item in ipairs(list) do
		if item.id == id then
			return item
		end
	end
	return nil
end

local function make_view(data)
	local view = read_only(data)
	local methods = {
		find_media = function(_, id)
			local item = find(data.media, id)
			return item and read_only(item)
		end,
		find_track = function(_, id)
			local item = find(data.tracks, id)
			return item and read_only(item)
		end,
		find_clip = function(_, id)
			local item = find(data.clips, id)
			return item and read_only(item)
		end,
		-- The clips that overlap the frames [from, to), on one track or on all.
		clips_in_range = function(_, from, to, track_id)
			local found = {}
			for _, clip in ipairs(data.clips) do
				local clip_end = clip.position + clip.out - clip["in"]
				if (track_id == nil or clip.trackId == track_id) and clip.position < to and clip_end > from then
					table.insert(found, read_only(clip))
				end
			end
			return found
		end,
		clips_of_media = function(_, media_id)
			local found = {}
			for _, clip in ipairs(data.clips) do
				if clip.mediaId == media_id then
					table.insert(found, read_only(clip))
				end
			end
			return found
		end,
	}
	local mt = getmetatable(view)
	local index = mt.__index
	mt.__index = function(t, key)
		return methods[key] or index(t, key)
	end
	mt.__metatable = view_mt
	return view
end

local cached

function ms.project()
	local session, revision = host.revision()
	if not cached or cached.session ~= session or cached.revision ~= revision then
		cached = make_view(host.snapshot())
	end
	return cached
end

-- Modules, from the plugin's own folder ------------------------------------------------------

local loaded = {}

function require(name)
	if type(name) ~= "string" then
		error("require needs a module name", 2)
	end
	if loaded[name] ~= nil then
		return loaded[name]
	end
	local source, problem = host.read_module(name)
	if not source then
		error(problem, 2)
	end
	local chunk, syntax = raw_load(source, "@" .. name:gsub("%.", "/") .. ".lua", "t")
	if not chunk then
		error(syntax, 2)
	end
	local result = chunk(name)
	loaded[name] = result == nil and true or result
	return loaded[name]
end

-- Text chunks only, in the plugin's environment.
function load(chunk, chunk_name)
	return raw_load(chunk, chunk_name, "t")
end

host.subscribe("job.finished")

return dispatch
