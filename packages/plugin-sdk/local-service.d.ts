// SPDX-License-Identifier: GPL-3.0-or-later
import type {PluginContext,Disposable} from './index';
import type {IncomingMessage,ServerResponse} from 'node:http';
export interface LocalService extends Disposable {readonly endpoint:string;readonly nonce:string;readonly baseURL:string;}
/** An ephemeral IPv4 loopback service owned by this activation, disposed with context.subscriptions. */
export function startLocalService(context:PluginContext,handler:(request:IncomingMessage,response:ServerResponse,path:string)=>void|Promise<void>):Promise<LocalService>;
