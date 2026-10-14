import { readFileSync } from 'node:fs';
const instructions=readFileSync(new URL('./card-prompt.txt',import.meta.url),'utf8').trim();
const schema = { type: 'object', additionalProperties: false, required: ['name','html','width','height'], properties: {
  name: {type:'string'}, html: {type:'string'}, width:{type:'integer'}, height:{type:'integer'} } };
export function validateCard(value) {
  if (!value || Object.keys(value).sort().join(',') !== 'height,html,name,width' ||
      typeof value.name !== 'string' || !value.name.trim() || value.name.length > 80 || /[\x00-\x1f]/.test(value.name) ||
      typeof value.html !== 'string' || !value.html.trim() || Buffer.byteLength(value.html) > 131072 ||
      !Number.isInteger(value.width) || value.width < 180 || value.width > 1280 ||
      !Number.isInteger(value.height) || value.height < 100 || value.height > 720) throw new Error('invalid_card');
  return { ...value, name:value.name.trim() };
}
export function generationProvider(config, fetchImpl = fetch) {
  return async prompt => {
    const response = await fetchImpl('https://api.openai.com/v1/responses', { method:'POST',
      headers:{Authorization:`Bearer ${config.openaiKey}`,'Content-Type':'application/json'},
      signal:AbortSignal.timeout(75000), body:JSON.stringify({model:config.aiModel,store:false,max_output_tokens:8000,
        ...(config.aiReasoningEffort?{reasoning:{effort:config.aiReasoningEffort}}:{}),instructions,
        input:prompt,text:{format:{type:'json_schema',name:'callayers_card',strict:true,schema}} }) });
    if (!response.ok) throw new Error('provider_unavailable');
    const text=await response.text();if(Buffer.byteLength(text)>1048576)throw new Error('provider_output_too_large');
    const result=JSON.parse(text);if(result.status!=='completed')throw new Error('provider_incomplete');
    const output=(result.output||[]).flatMap(item=>item.content||[]).filter(item=>item.type==='output_text').map(item=>item.text).join('');
    return validateCard(JSON.parse(output));
  };
}
export async function readGeneration(req) {
  if (!/^application\/json(?:;|$)/i.test(req.headers['content-type']||'')) throw Object.assign(new Error(),{status:415,code:'json_required'});
  let length=0,chunks=[];
  for await (const chunk of req) {length+=chunk.length;if(length>16384)throw Object.assign(new Error(),{status:413,code:'prompt_too_large'});chunks.push(chunk);}
  let value;try{value=JSON.parse(Buffer.concat(chunks).toString('utf8'));}catch{throw Object.assign(new Error(),{status:400,code:'invalid_request'});}
  if(!value || Object.keys(value).sort().join(',')!=='prompt,requestId' || typeof value.prompt!=='string' || !value.prompt.trim() ||
      Buffer.byteLength(value.prompt)>12000 || typeof value.requestId!=='string' || !/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value.requestId))
    throw Object.assign(new Error(),{status:400,code:'invalid_request'});
  if(/https?:\/\/|\bwww\.|(?<!@)\b[a-z0-9-]+\.(?:com|org|net|io|co|edu|gov)(?:\b|\/)/i.test(value.prompt))
    throw Object.assign(new Error(),{status:400,code:'website_tools_not_ready'});
  return value;
}
