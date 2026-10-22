import {Matrix4,Vector3,MathUtils} from 'three';

// Art-review travel and pivots in glTF metres, not a measured firearm mechanism.
export const POSES={
  Action:{travel:.055},
  Stock:{pivot:[-.012,.063,0],angle:110,deployedAngle:195},
  LoadingLid:{pivot:[.105,.055,0],angle:-80}
};
export function getPoseDelta(assembly,value,angleOverride){
  value=Math.min(1,Math.max(0,Number(value)||0));
  const pose=POSES[assembly];
  if(!pose)return new Matrix4();
  if(pose.travel!==undefined)return new Matrix4().makeTranslation(pose.travel*value,0,0);
  const p=new Vector3(...pose.pivot);
  return new Matrix4().makeTranslation(p.x,p.y,p.z)
    .multiply(new Matrix4().makeRotationZ(MathUtils.degToRad((angleOverride??pose.angle)*value)))
    .multiply(new Matrix4().makeTranslation(-p.x,-p.y,-p.z));
}
