(*
 * Copyright (C) 2006-2007 XenSource Ltd.
 * Copyright (C) 2008      Citrix Ltd.
 * Author Vincent Hanquez <vincent.hanquez@eu.citrix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *)

open Xenbus

exception Partial_not_empty
exception Unexpected_packet of string

(** Thrown when a path looks invalid e.g. if it contains "//" *)
exception Invalid_path of string

let unexpected_packet expected received =
	let s = Printf.sprintf "expecting %s received %s"
	                       (Xb.Op.to_string expected)
	                       (Xb.Op.to_string received) in
	raise (Unexpected_packet s)

type con = {
	xb: Xenbus.Xb.t;
	watchevents: (string * string) Queue.t;
}

let close con =
	Xb.close con.xb

let open_fd fd = {
	xb = Xb.open_fd fd;
	watchevents = Queue.create ();
}

let rec split_string ?limit:(limit=(-1)) c s =
	let i = try String.index s c with Not_found -> -1 in
	let nlimit = if limit = -1 || limit = 0 then limit else limit - 1 in
	if i = -1 || nlimit = 0 then
		[ s ]
	else
		let a = String.sub s 0 i
		and b = String.sub s (i + 1) (String.length s - i - 1) in
		a :: (split_string ~limit: nlimit c b)

type perm = PERM_NONE | PERM_READ | PERM_WRITE | PERM_RDWR

type perms = int * perm * (int * perm) list

let string_of_perms perms =
	let owner, other, acl = perms in
	let char_of_perm perm =
		match perm with PERM_NONE -> 'n' | PERM_READ -> 'r'
			      | PERM_WRITE -> 'w' | PERM_RDWR -> 'b' in
	let string_of_perm (id, perm) = Printf.sprintf "%c%u" (char_of_perm perm) id in
	String.concat "\000" (List.map string_of_perm ((owner,other) :: acl))

let perms_of_string s =
	let perm_of_char c =
		match c with 'n' -> PERM_NONE | 'r' -> PERM_READ
		           | 'w' -> PERM_WRITE | 'b' -> PERM_RDWR
		           | c -> invalid_arg (Printf.sprintf "unknown permission type: %c" c) in
	let perm_of_string s =
		if String.length s < 2 
		then invalid_arg (Printf.sprintf "perm of string: length = %d; contents=\"%s\"" (String.length s) s) 
		else
		begin
			int_of_string (String.sub s 1 (String.length s - 1)),
			perm_of_char s.[0]
		end in
	let rec split s =
		try let i = String.index s '\000' in
		String.sub s 0 i :: split (String.sub s (i + 1) (String.length s - 1 - i))
		with Not_found -> if s = "" then [] else [ s ] in
	let l = List.map perm_of_string (split s) in
	match l with h :: l -> (fst h, snd h, l) | [] -> (0, PERM_NONE, [])

(* send one packet - can sleep *)
let pkt_send con =
	if Xb.has_old_output con.xb then
		raise Partial_not_empty;
	let workdone = ref false in
	while not !workdone
	do
		workdone := Xb.output con.xb
	done

(* receive one packet - can sleep *)
let pkt_recv con =
	let workdone = ref false in
	while not !workdone
	do
		workdone := Xb.input con.xb
	done;
	Xb.get_in_packet con.xb

let pkt_recv_timeout con timeout =
	let fd = Xb.get_fd con.xb in
	let r, _, _ = Unix.select [ fd ] [] [] timeout in
	if r = [] then
		true, None
	else (
		let workdone = Xb.input con.xb in
		if workdone then
			false, (Some (Xb.get_in_packet con.xb))
		else
			false, None
	)

let queue_watchevent con data =
	let ls = split_string ~limit:2 '\000' data in
	if List.length ls != 2 then
		raise (Xb.Packet.DataError "arguments number mismatch");
	let event = List.nth ls 0
	and event_data = List.nth ls 1 in
	Queue.push (event, event_data) con.watchevents

let has_watchevents con = Queue.length con.watchevents > 0
let get_watchevent con = Queue.pop con.watchevents

let read_watchevent con =
	let pkt = pkt_recv con in
	match Xb.Packet.get_ty pkt with
	| Xb.Op.Watchevent ->
		queue_watchevent con (Xb.Packet.get_data pkt);
		Queue.pop con.watchevents
	| ty               -> unexpected_packet Xb.Op.Watchevent ty

(* send one packet in the queue, and wait for reply *)
let rec sync_recv ty con =
	let pkt = pkt_recv con in
	match Xb.Packet.get_ty pkt with
	| Xb.Op.Error       -> (
		match Xb.Packet.get_data pkt with
		| "ENOENT" -> raise Xb.Noent
		| "EAGAIN" -> raise Xb.Eagain
		| "EINVAL" -> raise Xb.Invalid
		| s        -> raise (Xb.Packet.Error s))
	| Xb.Op.Watchevent  ->
		queue_watchevent con (Xb.Packet.get_data pkt);
		sync_recv ty con
	| rty when rty = ty -> Xb.Packet.get_data pkt
	| rty               -> unexpected_packet ty rty

let sync f con =
	(* queue a query using function f *)
	f con.xb;
	if Xb.output_len con.xb = 0 then
		Printf.printf "output len = 0\n%!";
	let ty = Xb.Packet.get_ty (Xb.peek_output con.xb) in
	pkt_send con;
	sync_recv ty con

let ack s =
	if s = "OK" then () else raise (Xb.Packet.DataError s)

(** Check paths are suitable for read/write/mkdir/rm/directory etc (NOT watches) *)
let validate_path path =
	(* Paths shouldn't have a "//" in the middle *)
	let bad = "//" in
	for offset = 0 to String.length path - (String.length bad) do
		if String.sub path offset (String.length bad) = bad then
			raise (Invalid_path path)
	done;
	(* Paths shouldn't have a "/" at the end, except for the root *)
	if path <> "/" && path <> "" && path.[String.length path - 1] = '/' then
		raise (Invalid_path path)

(** Check to see if a path is suitable for watches *)
let validate_watch_path path =
	(* Check for stuff like @releaseDomain etc first *)
	if path <> "" && path.[0] = '@' then ()
	else validate_path path

let debug command con =
	sync (Queueop.debug command) con

let directory tid path con =
	validate_path path;
	let data = sync (Queueop.directory tid path) con in
	split_string '\000' data

let read tid path con =
	validate_path path;
	sync (Queueop.read tid path) con

let readv tid dir vec con =
	List.map (fun path -> validate_path path; read tid path con)
		(if dir <> "" then
			(List.map (fun v -> dir ^ "/" ^ v) vec) else vec)

let getperms tid path con =
	validate_path path;
	perms_of_string (sync (Queueop.getperms tid path) con)

let watch path data con =
	validate_watch_path path;
	ack (sync (Queueop.watch path data) con)

let unwatch path data con =
	validate_watch_path path;
	ack (sync (Queueop.unwatch path data) con)

let transaction_start con =
	let data = sync (Queueop.transaction_start) con in
	try
		int_of_string data
	with
		_ -> raise (Packet.DataError (Printf.sprintf "int expected; got '%s'" data))

let transaction_end tid commit con =
	try
		ack (sync (Queueop.transaction_end tid commit) con);
		true
	with
		Xb.Eagain -> false

let introduce domid mfn port con =
	ack (sync (Queueop.introduce domid mfn port) con)

let release domid con =
	ack (sync (Queueop.release domid) con)

let resume domid con =
	ack (sync (Queueop.resume domid) con)

let getdomainpath domid con =
	sync (Queueop.getdomainpath domid) con

let write tid path value con =
	validate_path path;
	ack (sync (Queueop.write tid path value) con)

let writev tid dir vec con =
	List.iter (fun (entry, value) ->
		let path = (if dir <> "" then dir ^ "/" ^ entry else entry) in
                validate_path path;
		write tid path value con) vec

let mkdir tid path con =
	validate_path path;
	ack (sync (Queueop.mkdir tid path) con)

let rm tid path con =
        validate_path path;
	try
		ack (sync (Queueop.rm tid path) con)
	with
		Xb.Noent -> ()

let setperms tid path perms con =
	validate_path path;
	ack (sync (Queueop.setperms tid path (string_of_perms perms)) con)

let setpermsv tid dir vec perms con =
	List.iter (fun entry ->
		let path = (if dir <> "" then dir ^ "/" ^ entry else entry) in
		validate_path path;
		setperms tid path perms con) vec
