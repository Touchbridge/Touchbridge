/**
 *
 * touchbridge.js
 *
 * This file is part of Touchbridge
 *
 * Copyright 2015 James L Macfarlane
 *
 * Based on IBM Raspberry Pi GPIO node.
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
 **/

module.exports = function (RED) {
    "use strict";
    var util = require("util");
    var exec = require('child_process').exec;
    var spawn = require('child_process').spawn;
    var fs =  require('fs');

    //var tbg_server = 'tcp://192.168.2.11:5555';
    var tbg_server = undefined;

    var boards = undefined

    var tbg_client = __dirname + '/tbg_client';
    //var tbg_client = '/usr/local/bin/tbg_client';

    // Update the coloured 'dot' & status information on the node
    // and write to log if in verbose mode.
    function update_status(node, data) {
        node.status({fill:"green",shape:"dot",text:data});
        if (RED.settings.verbose) { node.log("state: " + data); }
    }

    function check_bool_value(input) {
        if (input === "true") { input = true; }
        if (input === "false") { input = false; }
        var value = Number(input);
        if (value < 0) { value = 0; }
        if (value > 1) { value = 1; }
        return value;
    }

    function write_output(node, value) {
        if (node.child !== null) {
            // Write message payload to stdin of tbg_client process
            node.child.stdin.write(value + "\n");
            update_status(node, String(value));
        }
    }

    function write_hco_enable(node, value) {
        if (node.child !== null && value !== undefined) {
            node.enable = value;
            node.child.stdin.write('dout ' + node.addr + ' ' + node.pin + ' ' + String(value) + '\n');
            node.status({fill:"green",shape:"dot",text:parseFloat(node.pwm).toFixed(3) + ' ' + [ 'off', 'on' ][node.enable]});
            if (RED.settings.verbose) { node.log("enable: " + value); }
        }
    }

    function write_hco_pwm(node, value) {
        if (RED.settings.verbose) { node.log("write_hco_pwm: " + value); }
        if (node.child !== null && value !== undefined) {
            node.pwm = value;
            var pwm = Math.round(value * 1439.0); // TODO: get max pwm value from board config port.
            node.child.stdin.write('aout ' + node.addr + ' ' + node.pin + ' ' + String(pwm) + '\n');
            node.status({fill:"green",shape:"dot",text:parseFloat(node.pwm).toFixed(3) + ' ' + [ 'off', 'on' ][node.enable]});
            if (RED.settings.verbose) { node.log("pwm: " + value); }
        }
    }

    // Get address from id
    function get_board_address(card_id) {
        for (var key in boards) {
            var brd = boards[key];
            if (brd.id == card_id) {
                return brd.addr;
            }
        }
        return undefined;
    }

    function child_stderr(node, data) {
        if (RED.settings.verbose) { node.log("err: "+data+" :"); }
    }

    function child_close(node, code) {
        node.child = null;
        node.running = false;
        if (RED.settings.verbose) { node.log(RED._("touchbridge.status.closed")); }
        if (node.done) {
            node.status({fill:"grey",shape:"ring",text:"touchbridge.status.closed"});
            if (RED.settings.verbose) { node.log(RED._("touchbridge.status: calling done()")); }
            node.done();
        }
        else { node.status({fill:"red",shape:"ring",text:"touchbridge.status.stopped"}); }
    }

    function child_error (node, err) {
        console.log('child command error: ' + err);
        if (err.errno === "ENOENT") { node.error(RED._("touchbridge.errors.commandnotfound")); }
        else if (err.errno === "EACCES") { node.error(RED._("touchbridge.errors.commandnotexecutable")); }
        else { node.error(RED._("touchbridge.errors.error",{error:err.errno})); }
    }

    function node_close(node, done) {
        if (RED.settings.verbose) { node.log(RED._("touchbridge.status: node_close()")); }
        //node.status({fill:"grey",shape:"ring",text:"touchbridge.status.closed"});
        if (node.child != null) {
            node.done = done;
            node.closing = true;
            //node.child.stdin.write("close "+node.pin);
            node.child.kill('SIGTERM');
        }
        else {
            if (RED.settings.verbose) { node.log(RED._("touchbridge.status: no child process. Calling done()")); }
            done();
        }
    }

    // Returns zero and uodates node status indicator on error.
    function check_child_process(node) {
        if (node.child !== null) {
            return 1;
        } else {
            node.error(RED._("touchbridge.errors.clientnotrunning"),msg);
            node.status({fill:"red",shape:"ring",text:"touchbridge.status.not-running"});
            return 0;
        }
    }

    // Check if important parameters (such as pin and card id) are set.
    // Returns zero on error.
    function tbg_valid_params(node) {
        if (boards == undefined) {
            node.warn(RED._("touchbridge.errors.nocards"));
            return 0;
        }
        if (node.card == undefined) {
            node.warn(RED._("touchbridge.errors.invalidcard")+": "+node.card);
            return 0;
        }
        if (node.pin == undefined) {
            node.warn(RED._("touchbridge.errors.invalidpin")+": "+node.pin);
            return 0;
        }
        return 1;
    }

    // Set node's running flag and update status
    function node_running(node) {
        node.running = true;
        node.status({fill:"green",shape:"dot",text:"OK"});
    }

    function spawn_client(node, cmd, args) {
        if (node.tbg_server) {
            node.child = spawn(tbg_client, ['-s', node.tbg_server, cmd].concat(args));
        } else {
            node.child = spawn(tbg_client, [cmd].concat(args));
        }
    }

    /*
     * Touchbridge HCO Diagnostics Input
     */
    function TBG_HCO_DIAG_Node(n) {
        RED.nodes.createNode(this,n);
        this.addr = undefined;
        this.card = n.card;
        this.pin = n.pin;
        this.tbg_server = tbg_server;
        var node = this;
        node.closing = false;
        node.log(RED._("HCO DIAG: card:" + this.card + " pin: " + this.pin));

        // This function is a callback, called when messages arrive at our node
        function inputlistener(msg) {
            var value = Number(msg.payload);

            node.log(RED._("Message in value: " + value));

            if (node.child !== null) {
                // Write message payload to stdin of tbg_client process
                node.child.stdin.write(value + "\n");
            } else {
                node.log(RED._("Child is null"));
            }
        }


        // Node Initialisation
        if (tbg_valid_params(node)) {

            // Get address from id
            for (var key in boards) {
                var brd = boards[key];
                if (brd.id == node.card) {
                    node.addr = brd.addr;
                }
            }

            spawn_client(node, 'ain_stdio', [node.addr,node.pin]);

            node_running(node);

            // Callback for data comming from child process's stdout
            node.child.stdout.on('data', function (chunk) {
                // child.stdout is a Stream. The on('data'...) callback
                // gets objects, not strings, which is neat but baffling.
                // We need to convert it to a string then split on newline
                // as it seems we can get a 'chunk' which contains several
                // lines.
                var data = chunk.toString();
                node.log(RED._("Got stdin line: " + data));
                var lines = data.split('\n');
                // Send a message for each line so we don't miss state
                // updates from the input node. We only go to length-1 since
                // split() produces an empty string for inputs which end
                // with a separator character (which \n-terminated lines do!)
                for (var i = 0; i < lines.length-1; i++) {
                    var line = lines[i].trim();
                    var value = Number(line);
                    // Number() will return NaN if it doesn't recognise the
                    // supplied string, so we need to check for this.
                    if (!isNaN(value) && !node.closing) {
                        // FIXME: need to handle bus volts & temp conversions for ch's 9-11
                        value = 3.3 * (value-2048) / 2048; // Convert ADC counts to volts
                        value = value / 0.11; // Convert to amps
                        node.send({ topic:"tbg/"+node.pin, payload:value });
                        update_status(node, value);
                    }
                }
            });

            node.child.stderr.on('data', function (data) { child_stderr(node, data); });

            node.child.on('close', function (code) { child_close(node, code); });

            node.child.on('error', function (err) { child_error(node, err); });
            //
            // Register input message callback
            node.on("input", inputlistener);


        }

        node.on("close", function(done) { node_close(node, done); });

    }
    /*
     * Touchbridge Input
     */
    function TBG_INPUT_Node(n) {
        RED.nodes.createNode(this,n);
        this.addr = undefined;
        this.card = n.card;
        this.pin = n.pin;
        this.tbg_server = tbg_server;
        var node = this;
        node.closing = false;


        // Node Initialisation
        if (tbg_valid_params(node)) {

            // Get address from id
            for (var key in boards) {
                var brd = boards[key];
                if (brd.id == node.card) {
                    node.addr = brd.addr;
                }
            }

            spawn_client(node, 'din', [node.addr,node.pin]);

            node_running(node);

            node.child.stdout.on('data', function (chunk) {
                // child.stdout is a Stream. The on('data'...) callback
                // gets objects, not strings, which is neat but baffling.
                // We need to convert it to a string then split on newline
                // as it seems we can get a 'chunk' which contains several
                // lines.
                var data = chunk.toString();
                var lines = data.split('\n');
                // Send a message for each line so we don't miss state
                // updates from the input node. We only go to length-1 since
                // split() produces an empty string for inputs which end
                // with a separator character (which \n-terminated lines do!)
                for (var i = 0; i < lines.length-1; i++) {
                    var line = lines[i].trim();
                    var value = Number(line);
                    // Number() will return NaN if it doesn't recognise the
                    // supplied string, so we need to check for this.
                    // We also check if the the node close callback has been
                    // called (it sets node.closing) as if so, badness 10,000
                    // happens if we call node.send() for reasons I don't
                    // understand but it seems to have fixed the bug which
                    // crashes node-red on almost every re-deploy especially
                    // if sending edges into the intput card at a moderate rate
                    // (e.g. 10Hz.)
                    if (!isNaN(value) && !node.closing) {
                        node.send({ topic:"tbg/"+node.pin, payload:value });
                        update_status(node, value);
                    }
                }
            });

            node.child.stderr.on('data', function (data) { child_stderr(node, data); });

            node.child.on('close', function (code) { child_close(node, code); });

            node.child.on('error', function (err) { child_error(node, err); });

        }

        node.on("close", function(done) { node_close(node, done); });

    }


    /*
     * Touchbridge High-Side Output
     */
    function TBG_HSO_Node(n) {
        RED.nodes.createNode(this,n);
        this.addr = undefined;
        this.card = n.card;
        this.pin = n.pin;
        this.initial_level = n.initial_level || 0;
        this.tbg_server = tbg_server;
        var node = this;

        // This function is a callback, called when messages arrive at our node
        function inputlistener(msg) {
            var output_value = check_bool_value(msg.payload);

            write_output(node, output_value);
        }

        // Node Initialisation
        if (tbg_valid_params(node)) {

            // Get address from id
            node.addr = get_board_address(node.card);

            // Start the Touchbridge Client as a child process
            spawn_client(node, 'dout', [node.addr,node.pin]);

            // Set initial state of output
            var output_value = check_bool_value(node.initial_level);
            if (RED.settings.verbose) { node.log("TBG-HSO Initial state: " + output_value); }
            write_output(node, output_value);

            node_running(node);

            // Register our input listener callback
            node.on("input", inputlistener);

            node.child.stderr.on('data', function (data) { child_stderr(node, data); });

            node.child.on('close', function (code) { child_close(node, code); });

            node.child.on('error', function (err) { child_error(node, err); });

        }

        node.on("close", function(done) { node_close(node, done); });
    }

    /*
     * Touchbridge High Current Output
     */
    function TBG_HCO_Node(n) {
        RED.nodes.createNode(this,n);
        this.card = n.card;
        this.addr = undefined;
        this.pin = n.pin;
        this.set = n.set || false;
        this.initial_enable = n.initial_enable || 0;
        this.initial_pwm = n.initial_pwm || 0.0;
        this.tbg_server = tbg_server;
        this.enable = this.initial_enable;
        this.pwm = this.initial_pwm;
        var node = this;

        // This function is a callback, called when messages arrive at our node
        function inputlistener(msg) {
            var output_value = check_bool_value(msg.payload);

            if (msg.topic.toLowerCase() == 'pwm') {
                write_hco_pwm(node, output_value);
            } else {
                write_hco_enable(node, output_value);
            }
        }

        // Node Initialisation
        if (tbg_valid_params(node)) {

            // Get address from id
            node.addr = get_board_address(node.card);

            spawn_client(node, '-i', '');

            // Set initial enable state of output
            var enable = check_bool_value(node.initial_enable);
            write_hco_enable(node, enable);

            // Set initial PWM state of output
            var pwm = check_bool_value(node.initial_pwm);
            write_hco_pwm(node, pwm);

            node_running(node);

            // Register input message callback
            node.on("input", inputlistener);

            node.child.stderr.on('data', function (data) { child_stderr(node, data); });

            node.child.on('close', function (code) { child_close(node, code); });

            node.child.on('error', function (err) { child_error(node, err); });

        }

        node.on("close", function(done) {
            node_close(node, done);
        });
    }

    var server_str;
    if (tbg_server) {
        server_str = ' -s ' + tbg_server;
    } else {
        server_str = "";
    }
    exec(tbg_client + server_str + ' adisc2', function(err,stdout,stderr) {
        if (err) {
            console.log('Touchbridge Address Discovery Failed: ' + err);
        } else {
            try {
                boards = JSON.parse(stdout);
            } catch(err) {
                console.log('Touchbridge Address Discovery: JSON parse error: ' + err);
                console.log('Touchbridge Address Discovery: stdout was: ' + stdout);
                return;
            }
            for (var key in boards) {
                var brd = boards[key];
                console.log(brd.addr + ' : ' + brd.id + ' : ' + brd.product.id );
            }

            // We don't want the nodes to start until address discovery has been done
            // otherwise the setting of the initial state interfers with the process
            // by introducing a bunch of extra repsonse messages.
            // TODO: fix this, do something neater
            RED.nodes.registerType("TBG-INPUT", TBG_INPUT_Node);
            RED.nodes.registerType("TBG-HSO",TBG_HSO_Node);
            RED.nodes.registerType("TBG-HCO",TBG_HCO_Node);
            RED.nodes.registerType("TBG-HCO-DIAG",TBG_HCO_DIAG_Node);
        }
    });

    RED.httpAdmin.get('/tbg-boards/:id',function(req,res) {
        res.send( boards );
    });
}
