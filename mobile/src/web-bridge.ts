import {ControllerClient, commandText, type Snapshot} from '@nordic/controller-client';
export type WebRequest = {type:'request'; id:number; path:string; method:string; body?:string};
export type WebReply = {id:number; status:number; body?:unknown; error?:string};
export class WebBridge {
  private sequence = 0;
  constructor(readonly client: ControllerClient, readonly catalog: unknown) {}
  private status(): Snapshot {
    if (this.client.closed || !this.client.state) throw new Error('Bluetooth disconnected; reconnect');
    return this.client.state;
  }
  async handle(request: WebRequest): Promise<WebReply> {
    if (!Number.isSafeInteger(request.id) || request.id < 1 || request.type !== 'request') throw new Error('Invalid page request');
    const {client} = this;
    const snapshot = this.status();
    if (request.path === '/api/status' && request.method === 'GET') return {id:request.id,status:200,body:snapshot};
    if (request.path === '/api/workouts' && request.method === 'GET') return {id:request.id,status:200,body:this.catalog};
    if (request.path === '/api/session' && request.method === 'POST') {
      if (!client.token) throw new Error('Bluetooth session unavailable');
      this.sequence = 0;
      return {id:request.id,status:200,body:{token:client.token,client_id:client.clientId,control_epoch:client.epoch}};
    }
    if (request.path !== '/api/test/command' || request.method !== 'POST' || typeof request.body !== 'string') throw new Error('Unsupported page request');
    const parts = request.body.split(' ');
    const [version,token,seq,epoch,action,a,b] = parts;
    if (parts.length !== 7 || version !== 'v1' || token !== client.token || Number(seq) <= this.sequence ||
        commandText(token,Number(seq),Number(epoch),action,Number(a),Number(b)) !== request.body) throw new Error('Invalid page command');
    this.sequence = Number(seq);
    if (Number(epoch) !== client.epoch && !['stop','release','heartbeat'].includes(action)) {
      return {id:request.id,status:409,body:{...snapshot,ok:false,command_result:{reason:'stale_epoch'}}};
    }
    let reply;
    try { reply = await client.command(action,Number(a),Number(b)); }
    catch (error) {
      if (client.closed) throw error;
      return {id:request.id,status:409,body:{...this.status(),ok:false,command_result:{reason:error instanceof Error?error.message:'command_rejected'}}};
    }
    // The HTTP page expects the post-command snapshot, not a GATT acknowledgement.
    const deadline = Date.now()+1500;
    while (reply.revision && BigInt(this.status().revision)<BigInt(reply.revision)) {
      if (Date.now()>deadline) { client.invalidate(); throw new Error('Status confirmation timed out; reconnect'); }
      await new Promise(resolve=>setTimeout(resolve,20));
    }
    return {id:request.id,status:200,body:{...this.status(),ok:true,command_result:{reason:'accepted'}}};
  }
}
